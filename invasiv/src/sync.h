// tcp_file_transfer.hpp -> udp_file_transfer.hpp
// ---------------------------------------------------------------
// Single-header UDP file server + client + sync with openFrameworks
// Optimized: Uses UDP Blast + NACK for speed.
// Concurrency: Uses ephemeral port handoff to handle multiple clients.
// ---------------------------------------------------------------

#ifndef UDP_FILE_TRANSFER_HPP
#define UDP_FILE_TRANSFER_HPP

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <set>
#include <thread>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <map>
#include <mutex>
#include <deque>
#include <condition_variable>
#include <chrono>

// ------------------------------------------------------------
// openFrameworks: ofThread + ofEvent
// ------------------------------------------------------------

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
using socklen_t = int;
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

#include <ofThread.h>
#include <ofEvent.h>
#include <ofLog.h>
#include "MD5.h" 
#include "Coms.h" 

#define UPDATE_ME "UPDATE_ME"

namespace fs = std::filesystem;

// ------------------------------------------------------------
// UDP Configuration
// ------------------------------------------------------------
// MTU is typically 1500. IP Header (20) + UDP Header (8) = 28.
// Safe payload size ~1400 to avoid fragmentation.
constexpr int UDP_PACKET_SIZE = 1400;
constexpr int UDP_HEADER_SIZE = 9; // Type(1) + Seq(4) + Total(4)
constexpr int UDP_PAYLOAD_SIZE = UDP_PACKET_SIZE - UDP_HEADER_SIZE;
constexpr int MAX_RETRIES = 5;
constexpr int TIMEOUT_MS = 1000;

// ------------------------------------------------------------
// Winsock init/cleanup
// ------------------------------------------------------------
inline bool init_winsock() {
#ifdef _WIN32
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
    return true;
#endif
}
inline void cleanup_winsock() {
#ifdef _WIN32
    WSACleanup();
#endif
}

// ------------------------------------------------------------
// Protocol Commands
// ------------------------------------------------------------
constexpr uint8_t CMD_LIST = 1;
constexpr uint8_t CMD_GET = 2;
constexpr uint8_t CMD_PUT = 3;
constexpr uint8_t CMD_DELETE = 4;
constexpr uint8_t CMD_HANDOFF = 5; // Server -> Client: "Switch to this port"
constexpr uint8_t CMD_DATA = 10;
constexpr uint8_t CMD_NACK = 11; // Negative Acknowledgement
constexpr uint8_t CMD_DONE = 12;
constexpr uint8_t CMD_OK = 200;
constexpr uint8_t CMD_ERR = 255;

// ------------------------------------------------------------
// Progress event
// ------------------------------------------------------------
struct TransferProgress {
    enum Type { Upload, Download, Delete };
    Type type;
    std::string filename;
    uint64_t bytesTransferred = 0;
    uint64_t totalBytes = 0;
    float percent() const { return totalBytes > 0 ? (float)bytesTransferred / totalBytes : 1.0f; }
};

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------
struct UdpPacket {
    uint8_t type;
    uint32_t seq;   // Sequence number or Chunk ID
    uint32_t total; // Total chunks or data size
    uint8_t data[UDP_PAYLOAD_SIZE];
    int len;
};

inline void pack_header(uint8_t* buf, uint8_t type, uint32_t seq, uint32_t total) {
    buf[0] = type;
    uint32_t n_seq = htonl(seq);
    uint32_t n_tot = htonl(total);
    memcpy(buf + 1, &n_seq, 4);
    memcpy(buf + 5, &n_tot, 4);
}

inline void unpack_header(const uint8_t* buf, uint8_t& type, uint32_t& seq, uint32_t& total) {
    type = buf[0];
    uint32_t n_seq, n_tot;
    memcpy(&n_seq, buf + 1, 4);
    memcpy(&n_tot, buf + 5, 4);
    seq = ntohl(n_seq);
    total = ntohl(n_tot);
}

inline uint64_t hto64(uint64_t v) { return ((v & 0xFFULL) << 56) | ((v & 0xFF00ULL) << 40) | ((v & 0xFF0000ULL) << 24) | ((v & 0xFF000000ULL) << 8) | ((v & 0xFF00000000ULL) >> 8) | ((v & 0xFF0000000000ULL) >> 24) | ((v & 0xFF000000000000ULL) >> 40) | ((v & 0xFF00000000000000ULL) >> 56); }
inline uint64_t ntoh64(uint64_t v) { return hto64(v); }

// ------------------------------------------------------------
// Server
// ------------------------------------------------------------
class Server : public ofThread
{
public:
    ofEvent<TransferProgress> transferProgressEvent;

    Server(uint16_t port, const std::string &root_dir = ".")
        : m_port(port), m_root(fs::absolute(root_dir)) {
        if (!init_winsock()) throw std::runtime_error("Winsock init failed");
    }

    ~Server() {
        stopThread();
        cleanup_winsock();
    }

    void start() { startThread(); }
    void stop() { waitForThread(true); }

private:
    mutable std::mutex m_cache_mutex;
    mutable std::unordered_map<std::string, std::string> m_md5_cache;

    std::string get_cached_md5(const fs::path &full_path) const {
        std::string key = full_path.string();
        {
            std::lock_guard<std::mutex> tryLock(m_cache_mutex);
            auto it = m_md5_cache.find(key);
            if (it != m_md5_cache.end()) return it->second;
        }
        std::string hash;
        try {
            std::ifstream file(full_path, std::ios::binary);
            if (file) {
                file.seekg(0, std::ios::end);
                size_t sz = file.tellg();
                file.seekg(0);
                std::string data(sz, '\0');
                file.read(&data[0], sz);
                hash = MD5::hash(data);
            }
        } catch (...) {}
        {
            std::lock_guard<std::mutex> tryLock(m_cache_mutex);
            m_md5_cache[key] = hash;
        }
        return hash;
    }

    void invalidate_md5(const std::string &key) {
        std::lock_guard<std::mutex> tryLock(m_cache_mutex);
        m_md5_cache.erase(key);
    }

    // Main Listen Loop
    void threadedFunction() override {
        SOCKET listen_sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (listen_sock == INVALID_SOCKET) { ofLogError("udp_server") << "socket() failed"; return; }

        // Enable address reuse
        int opt = 1;
#ifdef _WIN32
        setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
#else
        setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(m_port);
        addr.sin_addr.s_addr = INADDR_ANY;

        if (::bind(listen_sock, (sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
            closesocket(listen_sock);
            ofLogError("udp_server") << "bind() failed";
            return;
        }

        ofLogNotice("udp_server") << "Listening on port " << m_port;

        std::vector<uint8_t> buffer(UDP_PACKET_SIZE);

        while (isThreadRunning()) {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(listen_sock, &readfds);
            timeval tv{0, 100000}; // 100ms timeout
            
            if (select(listen_sock + 1, &readfds, nullptr, nullptr, &tv) > 0) {
                sockaddr_in client_addr{};
                socklen_t client_len = sizeof(client_addr);
                int len = ::recvfrom(listen_sock, (char*)buffer.data(), UDP_PACKET_SIZE, 0, (sockaddr*)&client_addr, &client_len);

                if (len > UDP_HEADER_SIZE) {
                    // We received a command. 
                    // To support concurrency, we immediately spawn a detached handler on a NEW ephemeral port.
                    // We send the new port to the client.
                    ClientHandler* handler = new ClientHandler(m_root, this, client_addr, buffer, len);
                    handler->start();
                }
            }
        }
        closesocket(listen_sock);
    }

    friend class ClientHandler;

    // Handles a single client request on a dedicated ephemeral UDP socket
    class ClientHandler {
    public:
        ClientHandler(const fs::path &root, Server *parent, sockaddr_in client, const std::vector<uint8_t>& initial_pkt, int pkt_len)
            : m_root(root), m_parent(parent), m_client_addr(client), m_initial_pkt(initial_pkt), m_initial_len(pkt_len) {}

        ~ClientHandler() {
            if (m_sock != INVALID_SOCKET) closesocket(m_sock);
        }

        void start() {
            std::thread t(&ClientHandler::run, this);
            t.detach();
        }

        void run() {
            // 1. Create Ephemeral Socket
            m_sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            sockaddr_in bind_addr{};
            bind_addr.sin_family = AF_INET;
            bind_addr.sin_port = 0; // OS chooses port
            bind_addr.sin_addr.s_addr = INADDR_ANY;
            if (::bind(m_sock, (sockaddr*)&bind_addr, sizeof(bind_addr)) == SOCKET_ERROR) { delete this; return; }

            // 2. Get the port number
            socklen_t len = sizeof(bind_addr);
            getsockname(m_sock, (sockaddr*)&bind_addr, &len);
            uint16_t ephemeral_port = ntohs(bind_addr.sin_port);

            // 3. Send Handoff command to client (Reliable)
            uint8_t buf[UDP_HEADER_SIZE + 4];
            pack_header(buf, CMD_HANDOFF, 0, 4);
            uint32_t p_net = htonl(ephemeral_port);
            memcpy(buf + UDP_HEADER_SIZE, &p_net, 4);
            
            // Blast handoff a few times to ensure receipt (stateless reliability)
            for(int i=0; i<5; i++) {
                ::sendto(m_sock, (char*)buf, sizeof(buf), 0, (sockaddr*)&m_client_addr, sizeof(m_client_addr));
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }

            // 4. Process the initial request that triggered this handler
            // Decode initial packet
            uint8_t type; uint32_t seq, total;
            unpack_header(m_initial_pkt.data(), type, seq, total);
            std::string payload((char*)m_initial_pkt.data() + UDP_HEADER_SIZE, m_initial_len - UDP_HEADER_SIZE);

            if (type == CMD_LIST) list_directory();
            else if (type == CMD_GET) send_file(payload);
            else if (type == CMD_PUT) receive_file(payload);
            else if (type == CMD_DELETE) delete_file(payload);

            delete this;
        }

    private:
        void list_directory() {
            std::ostringstream oss;
            try {
                if(fs::exists(m_root)) {
                     for (const auto &entry : fs::recursive_directory_iterator(m_root, fs::directory_options::skip_permission_denied)) {
                        if (!entry.is_regular_file()) continue;
                        fs::path rel = entry.path().lexically_relative(m_root);
                        std::string rel_str = rel.string();
                        std::replace(rel_str.begin(), rel_str.end(), '\\', '/'); 
                        uint64_t fsize = entry.file_size();
                        std::string md5 = m_parent->get_cached_md5(entry.path());
                        oss << rel_str << '|' << fsize << '|' << md5 << '\n';
                    }
                }
            } catch(...) {}

            std::string data = oss.str();
            send_bulk_data((const uint8_t*)data.data(), data.size());
        }

        void send_file(const std::string &rel_path) {
            fs::path full = safe_path(rel_path, m_root);
            if (!fs::exists(full) || !fs::is_regular_file(full)) {
                send_error("File not found");
                return;
            }

            uint64_t fsize = fs::file_size(full);
            
            // Send CMD_OK and file size as a reliable control message
            uint64_t net_size = hto64(fsize);
            if (!send_ack_wait(CMD_OK, &net_size, 8)) return;

            // Send Data
            FILE *fp = fopen(full.string().c_str(), "rb");
            if (!fp) { send_error("Open failed"); return; }
            
            // Read entire file into memory for UDP blasting efficiency (assuming reasonably sized assets)
            // For massive files, chunking from disk would be better, but this is optimized for speed/code size.
            std::vector<uint8_t> file_buf(fsize);
            fread(file_buf.data(), 1, fsize, fp);
            fclose(fp);

            send_bulk_data(file_buf.data(), fsize, [&](size_t sent, size_t total){
                 TransferProgress prog;
                 prog.type = TransferProgress::Download;
                 prog.filename = rel_path;
                 prog.bytesTransferred = sent;
                 prog.totalBytes = total;
                 ofNotifyEvent(m_parent->transferProgressEvent, prog, m_parent);
            });
        }

        void receive_file(const std::string &rel_path) {
            fs::path full = safe_path(rel_path, m_root);
            if (full.has_parent_path() && !fs::exists(full.parent_path()))
                fs::create_directories(full.parent_path());

            // Acknowledge PUT request
            send_ack_simple(CMD_OK);

            // Wait for file size
            std::vector<uint8_t> sz_buf;
            if (!recv_ack_wait(sz_buf) || sz_buf.size() != 8) return;
            uint64_t fsize = ntoh64(*(uint64_t*)sz_buf.data());

            // Receive Data
            std::vector<uint8_t> data;
            if (receive_bulk_data(data, fsize, [&](size_t r, size_t t){
                TransferProgress prog;
                prog.type = TransferProgress::Upload;
                prog.filename = rel_path;
                prog.bytesTransferred = r;
                prog.totalBytes = t;
                ofNotifyEvent(m_parent->transferProgressEvent, prog, m_parent);
            })) {
                FILE *fp = fopen(full.string().c_str(), "wb");
                if (fp) {
                    fwrite(data.data(), 1, data.size(), fp);
                    fclose(fp);
                    m_parent->invalidate_md5(full.string());
                }
            }
        }

        void delete_file(const std::string &rel_path) {
            fs::path full = safe_path(rel_path, m_root);
            bool removed = false;
            if (fs::exists(full)) {
                std::error_code ec;
                fs::remove(full, ec);
                if (!ec) removed = true;
            }
            m_parent->invalidate_md5(full.string());
            if (removed) {
                send_ack_simple(CMD_OK);
                TransferProgress prog;
                prog.type = TransferProgress::Delete;
                prog.filename = rel_path;
                prog.bytesTransferred = 1; prog.totalBytes = 1;
                ofNotifyEvent(m_parent->transferProgressEvent, prog, m_parent);
            } else {
                send_error("Failed");
            }
        }

        // --- Low Level UDP Helpers ---

        void send_error(const std::string& msg) {
            send_ack_simple(CMD_ERR); // Simplified error
        }

        // Send a command and wait for a generic ACK (CMD_OK)
        bool send_ack_wait(uint8_t cmd, const void* data, size_t len) {
             // Implementation omitted for server side simplicity (usually server responds)
             // But server sometimes needs to handshake (e.g. sending filesize)
             return true; 
        }

        // Just send a status code blindly (for end of transaction)
        void send_ack_simple(uint8_t cmd) {
            uint8_t buf[UDP_HEADER_SIZE];
            pack_header(buf, cmd, 0, 0);
            ::sendto(m_sock, (char*)buf, UDP_HEADER_SIZE, 0, (sockaddr*)&m_client_addr, sizeof(m_client_addr));
        }

        // Receive reliable packet
        bool recv_ack_wait(std::vector<uint8_t>& out_data) {
             char buf[UDP_PACKET_SIZE];
             socklen_t slen = sizeof(m_client_addr);
             fd_set readfds; 
             timeval tv{2, 0}; // 2 sec timeout
             FD_ZERO(&readfds); FD_SET(m_sock, &readfds);
             
             if (select(m_sock+1, &readfds, nullptr, nullptr, &tv) > 0) {
                 int r = ::recvfrom(m_sock, buf, UDP_PACKET_SIZE, 0, (sockaddr*)&m_client_addr, &slen);
                 if (r > UDP_HEADER_SIZE) {
                     out_data.assign(buf + UDP_HEADER_SIZE, buf + r);
                     return true;
                 }
             }
             return false;
        }

        // Blast data + NACK handling
        bool send_bulk_data(const uint8_t* data, size_t size, std::function<void(size_t,size_t)> cb = nullptr) {
            uint32_t total_chunks = (size + UDP_PAYLOAD_SIZE - 1) / UDP_PAYLOAD_SIZE;
            
            // 1. Send Size Header
            uint64_t net_sz = hto64(size);
            uint8_t hbuf[UDP_HEADER_SIZE + 8];
            pack_header(hbuf, CMD_DATA, 0, total_chunks);
            memcpy(hbuf + UDP_HEADER_SIZE, &net_sz, 8);
            ::sendto(m_sock, (char*)hbuf, sizeof(hbuf), 0, (sockaddr*)&m_client_addr, sizeof(m_client_addr));
            
            // 2. Blast Loop
            for (uint32_t i = 0; i < total_chunks; ++i) {
                uint32_t offset = i * UDP_PAYLOAD_SIZE;
                uint32_t chunk_len = std::min<uint32_t>(UDP_PAYLOAD_SIZE, size - offset);
                
                uint8_t pkt[UDP_PACKET_SIZE];
                pack_header(pkt, CMD_DATA, i + 1, total_chunks); // Seq starts at 1
                memcpy(pkt + UDP_HEADER_SIZE, data + offset, chunk_len);
                
                ::sendto(m_sock, (char*)pkt, UDP_HEADER_SIZE + chunk_len, 0, (sockaddr*)&m_client_addr, sizeof(m_client_addr));
                
                // Slight delay to prevent local buffer overflow on fast networks
                if (i % 10 == 0) std::this_thread::sleep_for(std::chrono::microseconds(100)); 
            }
            if(cb) cb(size, size);

            // 3. Wait for NACKs or DONE
            // Loop until client sends CMD_DONE
            while (true) {
                // Send "End of transmission" probe
                uint8_t fin[UDP_HEADER_SIZE]; pack_header(fin, CMD_DONE, 0, 0);
                ::sendto(m_sock, (char*)fin, UDP_HEADER_SIZE, 0, (sockaddr*)&m_client_addr, sizeof(m_client_addr));

                char rbuf[UDP_PACKET_SIZE];
                socklen_t slen = sizeof(m_client_addr);
                fd_set rfd; FD_ZERO(&rfd); FD_SET(m_sock, &rfd);
                timeval tv{0, 200000}; // 200ms
                
                if (select(m_sock+1, &rfd, nullptr, nullptr, &tv) > 0) {
                    int n = ::recvfrom(m_sock, rbuf, UDP_PACKET_SIZE, 0, (sockaddr*)&m_client_addr, &slen);
                    if (n >= UDP_HEADER_SIZE) {
                        uint8_t type; uint32_t seq, tot;
                        unpack_header((uint8_t*)rbuf, type, seq, tot);
                        if (type == CMD_DONE) return true; // Success
                        if (type == CMD_NACK) {
                            // Resend requested chunk
                            // NACK payload is uint32_t sequence
                            uint32_t missing_seq;
                            memcpy(&missing_seq, rbuf + UDP_HEADER_SIZE, 4);
                            missing_seq = ntohl(missing_seq);
                            
                            if (missing_seq > 0 && missing_seq <= total_chunks) {
                                uint32_t idx = missing_seq - 1;
                                uint32_t offset = idx * UDP_PAYLOAD_SIZE;
                                uint32_t chunk_len = std::min<uint32_t>(UDP_PAYLOAD_SIZE, size - offset);
                                uint8_t pkt[UDP_PACKET_SIZE];
                                pack_header(pkt, CMD_DATA, missing_seq, total_chunks);
                                memcpy(pkt + UDP_HEADER_SIZE, data + offset, chunk_len);
                                ::sendto(m_sock, (char*)pkt, UDP_HEADER_SIZE + chunk_len, 0, (sockaddr*)&m_client_addr, sizeof(m_client_addr));
                            }
                        }
                    }
                }
            }
        }

        bool receive_bulk_data(std::vector<uint8_t>& out_data, uint64_t expected_size, std::function<void(size_t,size_t)> cb) {
            out_data.resize(expected_size);
            uint32_t total_chunks = (expected_size + UDP_PAYLOAD_SIZE - 1) / UDP_PAYLOAD_SIZE;
            std::vector<bool> received(total_chunks + 1, false);
            uint32_t received_count = 0;

            socklen_t slen = sizeof(m_client_addr);
            char buf[UDP_PACKET_SIZE];
            
            while (received_count < total_chunks) {
                fd_set rfd; FD_ZERO(&rfd); FD_SET(m_sock, &rfd);
                timeval tv{0, 500000}; // 500ms
                
                if (select(m_sock+1, &rfd, nullptr, nullptr, &tv) > 0) {
                    int n = ::recvfrom(m_sock, buf, UDP_PACKET_SIZE, 0, (sockaddr*)&m_client_addr, &slen);
                    if (n >= UDP_HEADER_SIZE) {
                        uint8_t type; uint32_t seq, tot;
                        unpack_header((uint8_t*)buf, type, seq, tot);
                        
                        if (type == CMD_DATA && seq > 0 && seq <= total_chunks) {
                            if (!received[seq]) {
                                received[seq] = true;
                                received_count++;
                                uint32_t offset = (seq - 1) * UDP_PAYLOAD_SIZE;
                                memcpy(out_data.data() + offset, buf + UDP_HEADER_SIZE, n - UDP_HEADER_SIZE);
                                if(cb && (received_count % 50 == 0)) cb(received_count * UDP_PAYLOAD_SIZE, expected_size);
                            }
                        } else if (type == CMD_DONE) {
                            // Sender thinks we are done, but we aren't.
                            // Trigger NACK loop below immediately
                        }
                    }
                } else {
                    // Timeout -> Send NACKs for missing
                    for (uint32_t i = 1; i <= total_chunks; ++i) {
                         if (!received[i]) {
                             uint8_t nack[UDP_HEADER_SIZE + 4];
                             pack_header(nack, CMD_NACK, 0, 0);
                             uint32_t ns = htonl(i);
                             memcpy(nack + UDP_HEADER_SIZE, &ns, 4);
                             ::sendto(m_sock, (char*)nack, sizeof(nack), 0, (sockaddr*)&m_client_addr, slen);
                             // Burst limit NACKs
                             if(i % 10 == 0) break; 
                         }
                    }
                }
            }
            
            // Send Done
            uint8_t fin[UDP_HEADER_SIZE]; pack_header(fin, CMD_DONE, 0, 0);
            for(int k=0; k<5; k++) ::sendto(m_sock, (char*)fin, sizeof(fin), 0, (sockaddr*)&m_client_addr, slen);
            
            return true;
        }

        fs::path safe_path(const std::string &rel, const fs::path &root) const {
            fs::path p = fs::path(rel).lexically_normal();
            if (p.is_absolute()) p = p.lexically_relative("/");
            return fs::absolute(root / p);
        }

        SOCKET m_sock = INVALID_SOCKET;
        fs::path m_root;
        Server *m_parent;
        sockaddr_in m_client_addr;
        std::vector<uint8_t> m_initial_pkt;
        int m_initial_len;
    };

    uint16_t m_port = 0;
    fs::path m_root;
};

// ------------------------------------------------------------
// Client (Synchronous)
// ------------------------------------------------------------
class Client
{
public:
    Client() { init_winsock(); }
    ~Client() { disconnect(); cleanup_winsock(); }

    bool connect(std::string host, uint16_t port) {
        disconnect();
        m_sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_sock == INVALID_SOCKET) return false;

        // Set generic timeout
        #ifdef _WIN32
        DWORD timeout = TIMEOUT_MS;
        setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
        #else
        struct timeval tv; tv.tv_sec = 0; tv.tv_usec = TIMEOUT_MS * 1000;
        setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(struct timeval));
        #endif

        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0) return false;
        
        memcpy(&m_server_addr, res->ai_addr, res->ai_addrlen);
        freeaddrinfo(res);
        return true;
    }

    void disconnect() {
        if (m_sock != INVALID_SOCKET) { closesocket(m_sock); m_sock = INVALID_SOCKET; }
    }

    std::unordered_map<std::string, std::pair<uint64_t, std::string>> list() {
        std::unordered_map<std::string, std::pair<uint64_t, std::string>> result;
        
        std::vector<uint8_t> data;
        if (!send_cmd_and_get_response(CMD_LIST, "", data)) return result;

        std::string sdata(data.begin(), data.end());
        std::istringstream iss(sdata);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.empty()) continue;
            size_t p1 = line.find('|'); size_t p2 = line.rfind('|');
            if (p1 == std::string::npos || p2 == std::string::npos || p1 == p2) continue;
            std::string path = line.substr(0, p1);
            std::string sizestr = line.substr(p1 + 1, p2 - p1 - 1);
            std::string md5 = line.substr(p2 + 1);
            uint64_t size = 0;
            try { size = std::stoull(sizestr); } catch(...) {}
            result[path] = {size, md5};
        }
        return result;
    }

    bool download(const std::string &remote, const std::string &local) {
        std::vector<uint8_t> data;
        // Sending CMD_GET triggers bulk receive
        if (!send_cmd_and_get_response(CMD_GET, remote, data)) return false; 
        
        FILE *fp = fopen(local.c_str(), "wb");
        if (!fp) return false;
        fwrite(data.data(), 1, data.size(), fp);
        fclose(fp);
        return true;
    }

    bool upload(const std::string &local, const std::string &remote) {
        if (!fs::exists(local)) return false;
        uint64_t fsize = fs::file_size(local);
        
        // 1. Send CMD_PUT to main server -> Handoff
        // 2. Wait for Ack on new port
        // 3. Send Size
        // 4. Blast Data
        
        sockaddr_in target = m_server_addr;
        if (!negotiate_handoff(CMD_PUT, remote, target)) return false;

        // Wait for server ready (CMD_OK)
        uint8_t buf[UDP_PACKET_SIZE];
        if(!wait_for_packet(target, buf, CMD_OK)) return false;

        // Send size
        uint64_t net_sz = hto64(fsize);
        ::sendto(m_sock, (char*)&net_sz, 8, 0, (sockaddr*)&target, sizeof(target));

        // Read file
        std::vector<uint8_t> file_buf(fsize);
        FILE *fp = fopen(local.c_str(), "rb");
        if(fp) { fread(file_buf.data(), 1, fsize, fp); fclose(fp); }
        else return false;

        return blast_data(target, file_buf.data(), fsize);
    }

    bool remove(const std::string &remote) {
        std::vector<uint8_t> dummy;
        return send_cmd_and_get_response(CMD_DELETE, remote, dummy);
    }

private:
    bool send_cmd_and_get_response(uint8_t cmd, const std::string& arg, std::vector<uint8_t>& out_data) {
        sockaddr_in target = m_server_addr;
        
        // 1. Negotiate Handoff: Send Cmd -> Get Handoff Port
        if (!negotiate_handoff(cmd, arg, target)) return false;

        // 2. Now talk to 'target'
        if (cmd == CMD_LIST || cmd == CMD_GET) {
            // Server will immediately start blasting data (or size header)
            return receive_bulk(target, out_data);
        }
        
        if (cmd == CMD_DELETE) {
            // Wait for simple ACK
             uint8_t buf[UDP_PACKET_SIZE];
             return wait_for_packet(target, buf, CMD_OK);
        }
        return false;
    }

    bool negotiate_handoff(uint8_t cmd, const std::string& arg, sockaddr_in& target) {
        int retries = 0;
        while(retries++ < 3) {
            // Send request to Main Server
            uint8_t buf[UDP_PACKET_SIZE];
            pack_header(buf, cmd, 0, 0);
            size_t len = UDP_HEADER_SIZE;
            if(!arg.empty()) {
                memcpy(buf + UDP_HEADER_SIZE, arg.data(), arg.size());
                len += arg.size();
            }
            ::sendto(m_sock, (char*)buf, len, 0, (sockaddr*)&m_server_addr, sizeof(m_server_addr));

            // Wait for HANDOFF
            char rx[UDP_PACKET_SIZE];
            socklen_t slen = sizeof(m_server_addr);
            
            fd_set rfd; FD_ZERO(&rfd); FD_SET(m_sock, &rfd);
            timeval tv{0, 500000}; // 500ms wait
            if (select(m_sock+1, &rfd, nullptr, nullptr, &tv) > 0) {
                int n = ::recvfrom(m_sock, rx, UDP_PACKET_SIZE, 0, (sockaddr*)&target, &slen); // update target address to new IP (same)
                if (n > UDP_HEADER_SIZE) {
                    uint8_t t; uint32_t s, tot;
                    unpack_header((uint8_t*)rx, t, s, tot);
                    if (t == CMD_HANDOFF) {
                         uint32_t p_net;
                         memcpy(&p_net, rx + UDP_HEADER_SIZE, 4);
                         target.sin_port = htons(ntohl(p_net)); // Set new ephemeral port
                         return true;
                    }
                }
            }
        }
        return false;
    }

    bool wait_for_packet(sockaddr_in& target, uint8_t* buf, uint8_t expected_type) {
        socklen_t slen = sizeof(target);
        fd_set rfd; FD_ZERO(&rfd); FD_SET(m_sock, &rfd);
        timeval tv{1, 0};
        if(select(m_sock+1, &rfd, nullptr, nullptr, &tv) > 0) {
            int n = ::recvfrom(m_sock, (char*)buf, UDP_PACKET_SIZE, 0, (sockaddr*)&target, &slen);
            if(n > 0 && buf[0] == expected_type) return true;
        }
        return false;
    }

    // Client-side Receiver Logic
    bool receive_bulk(sockaddr_in& target, std::vector<uint8_t>& out_data) {
        // 1. Wait for Header (CMD_DATA with Seq 0 or just CMD_OK + size? Server sends CMD_DATA w/ size)
        char buf[UDP_PACKET_SIZE];
        socklen_t slen = sizeof(target);
        
        // Retry loop for initial packet
        for(int i=0; i<5; i++) {
             fd_set rfd; FD_ZERO(&rfd); FD_SET(m_sock, &rfd);
             timeval tv{1, 0};
             if (select(m_sock+1, &rfd, nullptr, nullptr, &tv) > 0) {
                 int n = ::recvfrom(m_sock, buf, UDP_PACKET_SIZE, 0, (sockaddr*)&target, &slen);
                 if (n >= UDP_HEADER_SIZE) break;
             }
             // If timed out, might need to poke server again? 
             // Logic simplified: assumes handoff worked and server is sending.
        }

        uint8_t t; uint32_t s, tot;
        unpack_header((uint8_t*)buf, t, s, tot);
        
        // If it's a list/data, it might come as a stream
        uint64_t expected_size = 0;
        if (t == CMD_DATA && s == 0) {
            memcpy(&expected_size, buf + UDP_HEADER_SIZE, 8);
        } else if (t == CMD_OK) { // Sometimes server sends OK then size
             memcpy(&expected_size, buf + UDP_HEADER_SIZE, 8);
        } else {
            return false;
        }
        
        out_data.resize(expected_size);
        uint32_t total_chunks = (expected_size + UDP_PAYLOAD_SIZE - 1) / UDP_PAYLOAD_SIZE;
        std::vector<bool> received(total_chunks + 1, false);
        uint32_t received_count = 0;

        while(received_count < total_chunks) {
            fd_set rfd; FD_ZERO(&rfd); FD_SET(m_sock, &rfd);
            timeval tv{0, 200000}; 
            if (select(m_sock+1, &rfd, nullptr, nullptr, &tv) > 0) {
                int n = ::recvfrom(m_sock, buf, UDP_PACKET_SIZE, 0, (sockaddr*)&target, &slen);
                if (n >= UDP_HEADER_SIZE) {
                    unpack_header((uint8_t*)buf, t, s, tot);
                    if (t == CMD_DATA && s > 0 && s <= total_chunks) {
                         if (!received[s]) {
                             received[s] = true;
                             received_count++;
                             uint32_t offset = (s-1)*UDP_PAYLOAD_SIZE;
                             memcpy(out_data.data() + offset, buf + UDP_HEADER_SIZE, n - UDP_HEADER_SIZE);
                         }
                    } else if (t == CMD_DONE) { /* trigger nack */ }
                }
            } else {
                 // Send NACKS
                 for(uint32_t i=1; i<=total_chunks; ++i) {
                     if(!received[i]) {
                         uint8_t nack[UDP_HEADER_SIZE + 4];
                         pack_header(nack, CMD_NACK, 0, 0);
                         uint32_t ns = htonl(i);
                         memcpy(nack + UDP_HEADER_SIZE, &ns, 4);
                         ::sendto(m_sock, (char*)nack, sizeof(nack), 0, (sockaddr*)&target, slen);
                         if (i % 20 == 0) break; 
                     }
                 }
            }
        }
        
        uint8_t fin[UDP_HEADER_SIZE]; pack_header(fin, CMD_DONE, 0, 0);
        for(int k=0; k<3; k++) ::sendto(m_sock, (char*)fin, sizeof(fin), 0, (sockaddr*)&target, slen);
        return true;
    }

    // Client-side Sender Logic
    bool blast_data(sockaddr_in& target, const uint8_t* data, size_t size) {
        uint32_t total_chunks = (size + UDP_PAYLOAD_SIZE - 1) / UDP_PAYLOAD_SIZE;
        socklen_t slen = sizeof(target);

        // Blast
        for (uint32_t i = 0; i < total_chunks; ++i) {
            uint32_t offset = i * UDP_PAYLOAD_SIZE;
            uint32_t chunk_len = std::min<uint32_t>(UDP_PAYLOAD_SIZE, size - offset);
            uint8_t pkt[UDP_PACKET_SIZE];
            pack_header(pkt, CMD_DATA, i + 1, total_chunks);
            memcpy(pkt + UDP_HEADER_SIZE, data + offset, chunk_len);
            ::sendto(m_sock, (char*)pkt, UDP_HEADER_SIZE + chunk_len, 0, (sockaddr*)&target, slen);
            if (i % 20 == 0) std::this_thread::sleep_for(std::chrono::microseconds(50));
        }

        // Wait NACKs
        while(true) {
            // Ping Done
            uint8_t fin[UDP_HEADER_SIZE]; pack_header(fin, CMD_DONE, 0, 0);
            ::sendto(m_sock, (char*)fin, UDP_HEADER_SIZE, 0, (sockaddr*)&target, slen);

            char buf[UDP_PACKET_SIZE];
            fd_set rfd; FD_ZERO(&rfd); FD_SET(m_sock, &rfd);
            timeval tv{0, 200000};
            if(select(m_sock+1, &rfd, nullptr, nullptr, &tv) > 0) {
                 int n = ::recvfrom(m_sock, buf, UDP_PACKET_SIZE, 0, (sockaddr*)&target, &slen);
                 if (n >= UDP_HEADER_SIZE) {
                     uint8_t t; uint32_t s, tot;
                     unpack_header((uint8_t*)buf, t, s, tot);
                     if (t == CMD_DONE) return true;
                     if (t == CMD_NACK) {
                         uint32_t seq; memcpy(&seq, buf + UDP_HEADER_SIZE, 4); seq = ntohl(seq);
                         if (seq > 0 && seq <= total_chunks) {
                             uint32_t idx = seq - 1;
                             uint32_t offset = idx * UDP_PAYLOAD_SIZE;
                             uint32_t chunk_len = std::min<uint32_t>(UDP_PAYLOAD_SIZE, size - offset);
                             uint8_t pkt[UDP_PACKET_SIZE];
                             pack_header(pkt, CMD_DATA, seq, total_chunks);
                             memcpy(pkt + UDP_HEADER_SIZE, data + offset, chunk_len);
                             ::sendto(m_sock, (char*)pkt, UDP_HEADER_SIZE + chunk_len, 0, (sockaddr*)&target, slen);
                         }
                     }
                 }
            }
        }
    }

    SOCKET m_sock = INVALID_SOCKET;
    sockaddr_in m_server_addr{};
};

// ------------------------------------------------------------
// Sync Status
// ------------------------------------------------------------
struct SyncStatus {
    enum State { Connecting, Listing, Uploading, Deleting, Done, Error };
    State state;
    std::string host;
    uint16_t port;
    std::string filename;
    uint64_t bytes = 0;
    uint64_t total = 0;
    std::string message;
    float percent() const { return total > 0 ? (float)bytes / total : 0.0f; }
};

// ------------------------------------------------------------
// Threaded Sync Client
// ------------------------------------------------------------
class SyncClient : public ofThread
{
public:
    ofEvent<SyncStatus> syncEvent;
    fs::path m_local_root;

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_stop_requested{false};
    std::map<std::string, Peer> m_peers;
    std::map<std::string, bool> m_peer_synced_state;
    std::deque<std::string> m_pending_paths;
    std::unordered_map<std::string, std::pair<uint64_t, std::string>> m_local_cache;
    Client m_client; 

public:
    SyncClient(const std::string &local_root = ".") : m_local_root(fs::absolute(local_root)) {}
    ~SyncClient() { stop(); }

    void setPeers(const std::map<std::string, Peer>& peers) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_peers = peers;
            std::map<std::string, bool> new_synced_state;
            for(auto const& [key, peer] : m_peers) {
                if (m_peer_synced_state.count(key)) new_synced_state[key] = m_peer_synced_state[key];
                else new_synced_state[key] = false;
            }
            m_peer_synced_state = new_synced_state;
        }
        m_cv.notify_one(); 
    }

    void pathsHasUpdated(const std::vector<std::string> &paths, bool initialize = false) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (initialize) m_pending_paths.push_back(UPDATE_ME);
            for (const auto &p : paths) m_pending_paths.push_back(p);
        }
        m_cv.notify_one();
    }

    void start() {
        m_stop_requested = false;
        if (!isThreadRunning()) startThread();
    }

    void stop() {
        m_stop_requested = true;
        m_cv.notify_all();
        if (isThreadRunning()) waitForThread(true);
    }

private:
    void notify(const std::string ip, uint16_t port, SyncStatus::State state, const std::string &msg, uint64_t bytes = 0, uint64_t total = 0, const std::string &filename = "") {
        if(m_stop_requested) return;
        SyncStatus s; s.state = state; s.host = ip; s.port = port;
        s.filename = filename.empty() ? msg : filename;
        s.message = msg; s.bytes = bytes; s.total = total;
        ofNotifyEvent(syncEvent, s, this);
    }

    void process_cache_updates(std::deque<std::string>& updates) {
        while(!updates.empty()) {
            if(m_stop_requested) return;
            std::string path_str = updates.front(); updates.pop_front();
            if (path_str == UPDATE_ME) {
                m_local_cache.clear();
                 try {
                    if(fs::exists(m_local_root)) {
                         for (const auto &entry : fs::recursive_directory_iterator(m_local_root)) {
                            if (entry.is_regular_file()) updates.push_back(entry.path().string());
                         }
                    }
                } catch(...) {}
                continue;
            }
            fs::path entry = path_str;
            fs::path rel = entry.lexically_relative(m_local_root);
            std::string rel_str = rel.string();
            std::replace(rel_str.begin(), rel_str.end(), '\\', '/'); 
            if (!fs::exists(entry)) {
                m_local_cache.erase(rel_str);
            } else {
                std::string hash;
                try {
                    std::ifstream f(entry, std::ios::binary);
                    if (f) {
                        f.seekg(0, std::ios::end); auto sz = f.tellg(); f.seekg(0);
                        std::string buf(sz, '\0'); f.read(&buf[0], sz);
                        hash = MD5::hash(buf);
                    }
                } catch (...) {}
                uint64_t sz = 0; try { sz = fs::file_size(entry); } catch(...) {}
                m_local_cache[rel_str] = {sz, hash};
            }
        }
    }

    void threadedFunction() override {
        pathsHasUpdated({}, true);
        while (!m_stop_requested) {
            std::deque<std::string> local_path_updates;
            std::vector<std::pair<std::string, Peer>> peers_to_process;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait_for(lock, std::chrono::seconds(1), [&]{ return !m_pending_paths.empty() || m_stop_requested; });
                if (m_stop_requested) break;
                local_path_updates = std::move(m_pending_paths);
                bool has_new_updates = !local_path_updates.empty();
                for(const auto& [key, peer] : m_peers) {
                    bool is_synced = m_peer_synced_state[key];
                    if (!peer.is_self && (!is_synced || has_new_updates)) {
                        peers_to_process.push_back({key, peer});
                        if (has_new_updates) m_peer_synced_state[key] = false;
                    }
                }
            } 

            process_cache_updates(local_path_updates);

            bool any_success = false;
            for (const auto& item : peers_to_process) {
                if (m_stop_requested) break;
                std::string peerKey = item.first; Peer peer = item.second;
                notify(peer.ip, peer.syncPort, SyncStatus::Connecting, "Connecting UDP...");
                
                if (!m_client.connect(peer.ip, peer.syncPort)) {
                    notify(peer.ip, peer.syncPort, SyncStatus::Error, "Socket Fail");
                    continue; 
                }
                notify(peer.ip, peer.syncPort, SyncStatus::Connecting, "Connected");

                bool success = false;
                int attempts = 0;
                while(attempts < 3 && !m_stop_requested) {
                    attempts++;
                    bool changed = false;
                    auto remote_map = m_client.list();
                    
                    for (const auto &local_entry : m_local_cache) {
                        if (m_stop_requested) break;
                        const std::string &path = local_entry.first;
                        auto remote_it = remote_map.find(path);
                        bool need_upload = (remote_it == remote_map.end()) || (remote_it->second.second != local_entry.second.second);
                        if (need_upload) {
                            notify(peer.ip, peer.syncPort, SyncStatus::Uploading, path, 0, local_entry.second.first);
                            if (m_client.upload((m_local_root / path).string(), path)) {
                                changed = true;
                                notify(peer.ip, peer.syncPort, SyncStatus::Uploading, path, local_entry.second.first, local_entry.second.first);
                            } else notify(peer.ip, peer.syncPort, SyncStatus::Error, "Upload Fail");
                        }
                    }
                    for (const auto &remote_entry : remote_map) {
                        if (m_stop_requested) break;
                        const std::string &path = remote_entry.first;
                        if (m_local_cache.find(path) == m_local_cache.end()) {
                            notify(peer.ip, peer.syncPort, SyncStatus::Deleting, path);
                            if (m_client.remove(path)) {
                                changed = true;
                                notify(peer.ip, peer.syncPort, SyncStatus::Deleting, path, 1, 1);
                            } else notify(peer.ip, peer.syncPort, SyncStatus::Error, "Delete Fail");
                        }
                    }
                    if (!changed) { success = true; break; }
                }
                
                m_client.disconnect();
                if (success) {
                    notify(peer.ip, peer.syncPort, SyncStatus::Done, "Synced");
                    any_success = true;
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if(m_peer_synced_state.count(peerKey)) m_peer_synced_state[peerKey] = true;
                }
            }
            if (any_success && !m_stop_requested) {
                 SyncStatus done; done.state = SyncStatus::Done; done.message = "Cycle Complete"; 
                 ofNotifyEvent(syncEvent, done, this);
            }
        }
    }
};

#endif // UDP_FILE_TRANSFER_HPP