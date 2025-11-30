// udp_file_transfer.hpp
// ---------------------------------------------------------------
// Single-header UDP file server + client + sync with openFrameworks
// Fixed: Removed Keep-Alive spam (preventing Server thread exhaustion)
// Added: Smart Client Session Caching (Ping/Pong reuse vs Handshake)
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
#include <functional>

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
// UDP Config
// ------------------------------------------------------------
constexpr int UDP_PACKET_SIZE = 1400;
constexpr int UDP_HEADER_SIZE = 9; 
constexpr int UDP_PAYLOAD_SIZE = UDP_PACKET_SIZE - UDP_HEADER_SIZE;

// Timing Configuration
constexpr int SERVER_SESSION_TIMEOUT_MS = 10000; 
constexpr int SOCKET_RCV_TIMEOUT_MS = 1000;

// ------------------------------------------------------------
// Protocol Commands
// ------------------------------------------------------------
constexpr uint8_t CMD_HELLO = 1;     
constexpr uint8_t CMD_WELCOME = 2;   
constexpr uint8_t CMD_LIST = 3;
constexpr uint8_t CMD_GET = 4;
constexpr uint8_t CMD_PUT = 5;
constexpr uint8_t CMD_DELETE = 6;
constexpr uint8_t CMD_PING = 7;      // Check active session
constexpr uint8_t CMD_PONG = 8;      // Session alive
constexpr uint8_t CMD_SIZE = 9;
constexpr uint8_t CMD_DATA = 10;
constexpr uint8_t CMD_NACK = 11;
constexpr uint8_t CMD_DONE = 12;
constexpr uint8_t CMD_OK = 200;
constexpr uint8_t CMD_ERR = 255;

// ------------------------------------------------------------
// Structs & Helpers
// ------------------------------------------------------------
struct TransferProgress {
    enum Type { Upload, Download, Delete };
    Type type;
    std::string filename;
    uint64_t bytesTransferred = 0;
    uint64_t totalBytes = 0;
};

inline void init_winsock() {
#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}
inline void cleanup_winsock() {
#ifdef _WIN32
    WSACleanup();
#endif
}

inline void pack_header(uint8_t* buf, uint8_t type, uint32_t seq, uint32_t total) {
    buf[0] = type;
    uint32_t n_seq = htonl(seq); uint32_t n_tot = htonl(total);
    memcpy(buf + 1, &n_seq, 4); memcpy(buf + 5, &n_tot, 4);
}
inline void unpack_header(const uint8_t* buf, uint8_t& type, uint32_t& seq, uint32_t& total) {
    type = buf[0];
    uint32_t n_seq, n_tot;
    memcpy(&n_seq, buf + 1, 4); memcpy(&n_tot, buf + 5, 4);
    seq = ntohl(n_seq); total = ntohl(n_tot);
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
        : m_port(port), m_root(fs::absolute(root_dir)) { init_winsock(); }
    ~Server() { stopThread(); cleanup_winsock(); }

    void start() { startThread(); }
    void stop() { waitForThread(true); }

private:
    friend class ClientSession;
    mutable std::mutex m_cache_mutex;
    mutable std::unordered_map<std::string, std::string> m_md5_cache;

    std::string get_cached_md5(const fs::path &full_path) const {
        std::string key = full_path.string();
        {
            std::lock_guard<std::mutex> lock(m_cache_mutex);
            if (m_md5_cache.count(key)) return m_md5_cache[key];
        }
        std::string hash;
        try {
            std::ifstream file(full_path, std::ios::binary);
            if (file) {
                file.seekg(0, std::ios::end); size_t sz = file.tellg(); file.seekg(0);
                std::string data(sz, '\0'); file.read(&data[0], sz);
                hash = MD5::hash(data);
            }
        } catch (...) {}
        {
            std::lock_guard<std::mutex> lock(m_cache_mutex);
            m_md5_cache[key] = hash;
        }
        return hash;
    }
    void invalidate_md5(const std::string &key) {
        std::lock_guard<std::mutex> lock(m_cache_mutex);
        m_md5_cache.erase(key);
    }

    void threadedFunction() override {
        SOCKET listen_sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (listen_sock == INVALID_SOCKET) return;

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

        if (::bind(listen_sock, (sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) { closesocket(listen_sock); return; }

        ofLogNotice("UDP Server") << "Listening on " << m_port;

        while (isThreadRunning()) {
            fd_set readfds; FD_ZERO(&readfds); FD_SET(listen_sock, &readfds);
            timeval tv{0, 100000}; 
            
            if (select(listen_sock + 1, &readfds, nullptr, nullptr, &tv) > 0) {
                sockaddr_in client_addr{}; socklen_t client_len = sizeof(client_addr);
                char buf[UDP_PACKET_SIZE];
                int len = ::recvfrom(listen_sock, buf, UDP_PACKET_SIZE, 0, (sockaddr*)&client_addr, &client_len);

                if (len >= UDP_HEADER_SIZE) {
                    uint8_t type; uint32_t s, t; unpack_header((uint8_t*)buf, type, s, t);
                    if (type == CMD_HELLO) {
                        ClientSession* session = new ClientSession(m_root, this, client_addr);
                        session->start();
                    }
                }
            }
        }
        closesocket(listen_sock);
    }

    class ClientSession {
    public:
        ClientSession(const fs::path &root, Server *parent, sockaddr_in client)
            : m_root(root), m_parent(parent), m_client_addr(client) {}
        ~ClientSession() { if (m_sock != INVALID_SOCKET) closesocket(m_sock); }

        void start() { std::thread t(&ClientSession::run, this); t.detach(); }

        void run() {
            m_sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            sockaddr_in bind_addr{};
            bind_addr.sin_family = AF_INET;
            bind_addr.sin_port = 0; bind_addr.sin_addr.s_addr = INADDR_ANY;
            if (::bind(m_sock, (sockaddr*)&bind_addr, sizeof(bind_addr)) == SOCKET_ERROR) { delete this; return; }

            socklen_t len = sizeof(bind_addr);
            getsockname(m_sock, (sockaddr*)&bind_addr, &len);
            uint16_t ephemeral_port = ntohs(bind_addr.sin_port);

            send_welcome(ephemeral_port);

            char buf[UDP_PACKET_SIZE];
            socklen_t clen = sizeof(m_client_addr);
            
            while(true) {
                fd_set rfd; FD_ZERO(&rfd); FD_SET(m_sock, &rfd);
                timeval tv{SERVER_SESSION_TIMEOUT_MS / 1000, 0}; 

                if (select(m_sock+1, &rfd, nullptr, nullptr, &tv) > 0) {
                    int n = ::recvfrom(m_sock, buf, UDP_PACKET_SIZE, 0, (sockaddr*)&m_client_addr, &clen);
                    if (n >= UDP_HEADER_SIZE) {
                        uint8_t type; uint32_t seq, tot; unpack_header((uint8_t*)buf, type, seq, tot);
                        std::string payload((char*)buf + UDP_HEADER_SIZE, n - UDP_HEADER_SIZE);

                        if (type == CMD_HELLO) send_welcome(ephemeral_port); 
                        else if (type == CMD_PING) send_ack(CMD_PONG);
                        else if (type == CMD_LIST) list_directory();
                        else if (type == CMD_GET) send_file(payload);
                        else if (type == CMD_PUT) receive_file(payload);
                        else if (type == CMD_DELETE) delete_file(payload);
                    }
                } else {
                    break; 
                }
            }
            delete this;
        }

    private:
        void send_welcome(uint16_t port) {
            uint8_t b[UDP_HEADER_SIZE + 4]; pack_header(b, CMD_WELCOME, 0, 0);
            uint32_t p = htonl(port); memcpy(b + UDP_HEADER_SIZE, &p, 4);
            for(int i=0; i<3; i++) {
                ::sendto(m_sock, (char*)b, sizeof(b), 0, (sockaddr*)&m_client_addr, sizeof(m_client_addr));
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }

        void list_directory() {
            std::ostringstream oss;
            try {
                if(fs::exists(m_root)) {
                     for (const auto &entry : fs::recursive_directory_iterator(m_root, fs::directory_options::skip_permission_denied)) {
                        if (!entry.is_regular_file()) continue;
                        fs::path rel = entry.path().lexically_relative(m_root);
                        std::string rel_str = rel.string();
                        std::replace(rel_str.begin(), rel_str.end(), '\\', '/'); 
                        oss << rel_str << '|' << entry.file_size() << '|' << m_parent->get_cached_md5(entry.path()) << '\n';
                    }
                }
            } catch(...) {}
            std::string d = oss.str();
            send_bulk_data((const uint8_t*)d.data(), d.size());
        }

        void send_file(const std::string &rel) {
            fs::path full = safe_path(rel, m_root);
            if (!fs::exists(full) || !fs::is_regular_file(full)) { send_ack(CMD_ERR); return; }
            uint64_t fsize = fs::file_size(full);
            uint64_t nsz = hto64(fsize);
            uint8_t h[UDP_HEADER_SIZE + 8]; pack_header(h, CMD_SIZE, 0, 0); memcpy(h + UDP_HEADER_SIZE, &nsz, 8);
            ::sendto(m_sock, (char*)h, sizeof(h), 0, (sockaddr*)&m_client_addr, sizeof(m_client_addr));
            std::this_thread::sleep_for(std::chrono::milliseconds(2));

            FILE *fp = fopen(full.string().c_str(), "rb");
            if (!fp) return;
            std::vector<uint8_t> buf(fsize); fread(buf.data(), 1, fsize, fp); fclose(fp);

            send_bulk_data(buf.data(), fsize, [&](size_t s, size_t t){
                 TransferProgress p{TransferProgress::Download, rel, s, t};
                 ofNotifyEvent(m_parent->transferProgressEvent, p, m_parent);
            });
        }

        void receive_file(const std::string &rel) {
            send_ack(CMD_OK); 
            
            uint64_t fsize = 0;
            char b[UDP_PACKET_SIZE]; socklen_t len = sizeof(m_client_addr);
            auto start = std::chrono::steady_clock::now();
            bool got_size = false;

            while(std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
                 fd_set rf; FD_ZERO(&rf); FD_SET(m_sock, &rf); timeval tv{0, 200000};
                 if(select(m_sock+1, &rf, nullptr, nullptr, &tv) > 0) {
                     int n = ::recvfrom(m_sock, b, UDP_PACKET_SIZE, 0, (sockaddr*)&m_client_addr, &len);
                     if (n >= UDP_HEADER_SIZE) {
                         uint8_t type; uint32_t s,t; unpack_header((uint8_t*)b, type, s, t);
                         if (type == CMD_SIZE && n >= UDP_HEADER_SIZE + 8) {
                             memcpy(&fsize, b + UDP_HEADER_SIZE, 8); 
                             fsize = ntoh64(fsize);
                             got_size = true;
                             break;
                         }
                     }
                 }
            }

            if (!got_size) { return; }

            std::vector<uint8_t> data;
            if (receive_bulk_data(data, fsize, [&](size_t r, size_t t){
                TransferProgress p{TransferProgress::Upload, rel, r, t};
                ofNotifyEvent(m_parent->transferProgressEvent, p, m_parent);
            })) {
                fs::path full = safe_path(rel, m_root);
                if(full.has_parent_path()) fs::create_directories(full.parent_path());
                FILE* fp = fopen(full.string().c_str(), "wb");
                if(fp) { fwrite(data.data(), 1, data.size(), fp); fclose(fp); m_parent->invalidate_md5(full.string()); }
            }
        }

        void delete_file(const std::string &rel) {
            fs::path full = safe_path(rel, m_root);
            std::error_code ec; fs::remove(full, ec);
            m_parent->invalidate_md5(full.string());
            send_ack(CMD_OK);
            TransferProgress p{TransferProgress::Delete, rel, 1, 1};
            ofNotifyEvent(m_parent->transferProgressEvent, p, m_parent);
        }

        void send_ack(uint8_t cmd) {
            uint8_t b[UDP_HEADER_SIZE]; pack_header(b, cmd, 0, 0);
            ::sendto(m_sock, (char*)b, sizeof(b), 0, (sockaddr*)&m_client_addr, sizeof(m_client_addr));
        }

        bool send_bulk_data(const uint8_t* data, size_t size, std::function<void(size_t,size_t)> cb = nullptr) {
            uint32_t chunks = (size + UDP_PAYLOAD_SIZE - 1) / UDP_PAYLOAD_SIZE;
            uint64_t net_sz = hto64(size);
            uint8_t h[UDP_HEADER_SIZE + 8]; pack_header(h, CMD_DATA, 0, chunks); memcpy(h+UDP_HEADER_SIZE, &net_sz, 8);
            ::sendto(m_sock, (char*)h, sizeof(h), 0, (sockaddr*)&m_client_addr, sizeof(m_client_addr));

            for (uint32_t i = 0; i < chunks; ++i) {
                uint32_t off = i * UDP_PAYLOAD_SIZE;
                uint32_t len = std::min<uint32_t>(UDP_PAYLOAD_SIZE, size - off);
                uint8_t p[UDP_PACKET_SIZE]; pack_header(p, CMD_DATA, i + 1, chunks);
                memcpy(p + UDP_HEADER_SIZE, data + off, len);
                ::sendto(m_sock, (char*)p, UDP_HEADER_SIZE + len, 0, (sockaddr*)&m_client_addr, sizeof(m_client_addr));
                if (i % 15 == 0) std::this_thread::sleep_for(std::chrono::microseconds(50));
            }
            if(cb) cb(size, size);

            while (true) {
                uint8_t fin[UDP_HEADER_SIZE]; pack_header(fin, CMD_DONE, 0, 0);
                ::sendto(m_sock, (char*)fin, sizeof(fin), 0, (sockaddr*)&m_client_addr, sizeof(m_client_addr));

                char r[UDP_PACKET_SIZE]; socklen_t sl = sizeof(m_client_addr);
                fd_set rf; FD_ZERO(&rf); FD_SET(m_sock, &rf); timeval tv{0, 200000};
                if (select(m_sock+1, &rf, nullptr, nullptr, &tv) > 0) {
                    int n = ::recvfrom(m_sock, r, sizeof(r), 0, (sockaddr*)&m_client_addr, &sl);
                    if (n >= UDP_HEADER_SIZE) {
                        uint8_t t; uint32_t s, tot; unpack_header((uint8_t*)r, t, s, tot);
                        if (t == CMD_DONE) return true;
                        if (t == CMD_NACK) {
                             uint32_t m_seq; memcpy(&m_seq, r + UDP_HEADER_SIZE, 4); m_seq = ntohl(m_seq);
                             if (m_seq > 0 && m_seq <= chunks) {
                                 uint32_t off = (m_seq-1)*UDP_PAYLOAD_SIZE;
                                 uint32_t len = std::min<uint32_t>(UDP_PAYLOAD_SIZE, size - off);
                                 uint8_t p[UDP_PACKET_SIZE]; pack_header(p, CMD_DATA, m_seq, chunks);
                                 memcpy(p+UDP_HEADER_SIZE, data+off, len);
                                 ::sendto(m_sock, (char*)p, UDP_HEADER_SIZE+len, 0, (sockaddr*)&m_client_addr, sl);
                             }
                        }
                    }
                }
            }
        }

        bool receive_bulk_data(std::vector<uint8_t>& out, uint64_t size, std::function<void(size_t,size_t)> cb) {
            out.resize(size);
            uint32_t chunks = (size + UDP_PAYLOAD_SIZE - 1) / UDP_PAYLOAD_SIZE;
            std::vector<bool> rx(chunks + 1, false);
            uint32_t cnt = 0;
            char b[UDP_PACKET_SIZE]; socklen_t sl = sizeof(m_client_addr);

            while(cnt < chunks) {
                fd_set rf; FD_ZERO(&rf); FD_SET(m_sock, &rf); timeval tv{0, 500000};
                if (select(m_sock+1, &rf, nullptr, nullptr, &tv) > 0) {
                    int n = ::recvfrom(m_sock, b, sizeof(b), 0, (sockaddr*)&m_client_addr, &sl);
                    if(n >= UDP_HEADER_SIZE) {
                        uint8_t t; uint32_t s, tot; unpack_header((uint8_t*)b, t, s, tot);
                        if (t == CMD_DATA && s > 0 && s <= chunks && !rx[s]) {
                            rx[s] = true; cnt++;
                            memcpy(out.data() + (s-1)*UDP_PAYLOAD_SIZE, b + UDP_HEADER_SIZE, n - UDP_HEADER_SIZE);
                            if(cb && cnt%50==0) cb(cnt*UDP_PAYLOAD_SIZE, size);
                        }
                    }
                } else {
                    for(uint32_t i=1; i<=chunks; ++i) {
                        if(!rx[i]) {
                             uint8_t n[UDP_HEADER_SIZE+4]; pack_header(n, CMD_NACK, 0, 0);
                             uint32_t ns = htonl(i); memcpy(n+UDP_HEADER_SIZE, &ns, 4);
                             ::sendto(m_sock, (char*)n, sizeof(n), 0, (sockaddr*)&m_client_addr, sl);
                             if(i%20==0) break;
                        }
                    }
                }
            }
            uint8_t f[UDP_HEADER_SIZE]; pack_header(f, CMD_DONE, 0, 0);
            for(int i=0;i<3;i++) ::sendto(m_sock, (char*)f, sizeof(f), 0, (sockaddr*)&m_client_addr, sl);
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
    };

    uint16_t m_port;
    fs::path m_root;
};

// ------------------------------------------------------------
// Client
// ------------------------------------------------------------
class Client {
public:
    Client() { init_winsock(); }
    ~Client() { disconnect(); cleanup_winsock(); }

    bool connect(std::string host, uint16_t port) {
        if (m_sock == INVALID_SOCKET) {
            m_sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            #ifdef _WIN32
            DWORD t = SOCKET_RCV_TIMEOUT_MS; setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&t, sizeof(t));
            #else
            struct timeval tv{0, SOCKET_RCV_TIMEOUT_MS*1000}; setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
            #endif
        }

        // Get Main Port Addr
        addrinfo hints{}, *res; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0) return false;
        m_server_main_addr = *(sockaddr_in*)res->ai_addr;
        freeaddrinfo(res);

        // Session Key: IP:Port
        std::string key = host + ":" + std::to_string(port);
        
        // 1. Try to resume existing session
        if(m_sessions.count(key)) {
            m_session_addr = m_sessions[key];
            if (ping_session()) {
                m_connected = true;
                return true; 
            }
            // Ping failed? Server thread died. Fallback to handshake.
            m_sessions.erase(key);
        }

        // 2. Full Handshake
        if (handshake()) {
            m_sessions[key] = m_session_addr;
            return true;
        }
        return false;
    }

    void disconnect() {
        // We do not close the socket to keep ephemeral mappings, 
        // but if we must close, we clear sessions.
        // Logic change: Keep socket open to maintain cached sessions.
        // Only close in Destructor.
    }

    std::unordered_map<std::string, std::pair<uint64_t, std::string>> list() {
        std::unordered_map<std::string, std::pair<uint64_t, std::string>> res;
        if (!m_connected) return res;
        std::vector<uint8_t> d;
        if (!send_cmd_reliable(CMD_LIST, "", d)) return res;
        std::string s(d.begin(), d.end()); std::istringstream iss(s); std::string line;
        while (std::getline(iss, line)) {
            size_t p1 = line.find('|'), p2 = line.rfind('|');
            if (p1 == std::string::npos || p1 == p2) continue;
            try { res[line.substr(0, p1)] = {std::stoull(line.substr(p1+1, p2-p1-1)), line.substr(p2+1)}; } catch(...) {}
        }
        return res;
    }

    bool download(const std::string &remote, const std::string &local) {
        if (!m_connected) return false;
        std::vector<uint8_t> data;
        if (!send_cmd_reliable(CMD_GET, remote, data)) return false; 
        FILE *fp = fopen(local.c_str(), "wb"); if (!fp) return false;
        fwrite(data.data(), 1, data.size(), fp); fclose(fp);
        return true;
    }

    bool upload(const std::string &local, const std::string &remote) {
        if (!m_connected || !fs::exists(local)) return false;
        uint64_t fsize = fs::file_size(local);
        
        if(!send_header_wait_ack(CMD_PUT, remote)) {
             // If immediate fail, try one handshake retry?
             // Assuming connect() handled the session check, this fail implies network drop
             return false; 
        }

        uint64_t net_sz = hto64(fsize);
        uint8_t h[UDP_HEADER_SIZE + 8]; pack_header(h, CMD_SIZE, 0, 0); memcpy(h+UDP_HEADER_SIZE, &net_sz, 8);
        ::sendto(m_sock, (char*)h, sizeof(h), 0, (sockaddr*)&m_session_addr, sizeof(m_session_addr));

        std::vector<uint8_t> file_buf(fsize);
        FILE *fp = fopen(local.c_str(), "rb"); if(!fp) return false;
        fread(file_buf.data(), 1, fsize, fp); fclose(fp);

        return blast_data(file_buf.data(), fsize);
    }

    bool remove(const std::string &remote) {
        if (!m_connected) return false;
        std::vector<uint8_t> dummy;
        return send_cmd_reliable(CMD_DELETE, remote, dummy);
    }

private:
    bool m_connected = false;
    SOCKET m_sock = INVALID_SOCKET;
    sockaddr_in m_server_main_addr{};
    sockaddr_in m_session_addr{};
    std::unordered_map<std::string, sockaddr_in> m_sessions;

    bool ping_session() {
        uint8_t h[UDP_HEADER_SIZE]; pack_header(h, CMD_PING, 0, 0);
        ::sendto(m_sock, (char*)h, sizeof(h), 0, (sockaddr*)&m_session_addr, sizeof(m_session_addr));
        // Short timeout for ping
        return wait_for_packet(CMD_PONG, 200);
    }

    bool handshake() {
        m_session_addr = m_server_main_addr; 
        m_connected = false;
        for(int i=0; i<3; i++) {
            uint8_t h[UDP_HEADER_SIZE]; pack_header(h, CMD_HELLO, 0, 0);
            ::sendto(m_sock, (char*)h, sizeof(h), 0, (sockaddr*)&m_server_main_addr, sizeof(m_server_main_addr));
            if (wait_for_packet(CMD_WELCOME, 500)) return true;
        }
        return false;
    }

    bool send_cmd_reliable(uint8_t cmd, const std::string& arg, std::vector<uint8_t>& out_data) {
        if (!send_request(cmd, arg)) return false;
        if (cmd == CMD_LIST || cmd == CMD_GET) return recv_bulk(out_data);
        if (cmd == CMD_DELETE) return wait_for_packet(CMD_OK, 1000);
        return false;
    }

    bool send_header_wait_ack(uint8_t cmd, const std::string& arg) {
        if(!send_request(cmd, arg)) return false; 
        return wait_for_packet(CMD_OK, 1000);
    }

    bool send_request(uint8_t cmd, const std::string& arg) {
        uint8_t buf[UDP_PACKET_SIZE]; pack_header(buf, cmd, 0, 0);
        size_t len = UDP_HEADER_SIZE;
        if(!arg.empty()) { memcpy(buf + UDP_HEADER_SIZE, arg.data(), arg.size()); len += arg.size(); }
        ::sendto(m_sock, (char*)buf, len, 0, (sockaddr*)&m_session_addr, sizeof(m_session_addr));
        return true; 
    }

    bool wait_for_packet(uint8_t expected_type, int timeout_ms) {
        auto start = std::chrono::steady_clock::now();
        char buf[UDP_PACKET_SIZE]; 
        socklen_t slen = sizeof(m_session_addr);

        while(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(timeout_ms)) {
            fd_set rfd; FD_ZERO(&rfd); FD_SET(m_sock, &rfd); timeval tv{0, 100000};
            if(select(m_sock+1, &rfd, nullptr, nullptr, &tv) > 0) {
                int n = ::recvfrom(m_sock, buf, UDP_PACKET_SIZE, 0, (sockaddr*)&m_session_addr, &slen);
                if(n >= UDP_HEADER_SIZE) {
                    uint8_t t; uint32_t s, tot; unpack_header((uint8_t*)buf, t, s, tot);
                    if (t == expected_type) {
                        if (expected_type == CMD_WELCOME) {
                             uint32_t p; memcpy(&p, buf+UDP_HEADER_SIZE, 4);
                             m_session_addr = m_server_main_addr;
                             m_session_addr.sin_port = htons(ntohl(p));
                             m_connected = true;
                        }
                        return true;
                    }
                }
            }
        }
        return false;
    }

    bool recv_bulk(std::vector<uint8_t>& out_data) {
        char buf[UDP_PACKET_SIZE]; socklen_t slen = sizeof(m_session_addr);
        uint64_t expected_size = 0;
        bool got_header = false;
        auto start = std::chrono::steady_clock::now();

        while(std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
            fd_set rfd; FD_ZERO(&rfd); FD_SET(m_sock, &rfd); timeval tv{0, 200000};
            if (select(m_sock+1, &rfd, nullptr, nullptr, &tv) > 0) {
                 int n = ::recvfrom(m_sock, buf, UDP_PACKET_SIZE, 0, (sockaddr*)&m_session_addr, &slen);
                 if (n >= UDP_HEADER_SIZE) {
                     uint8_t t; uint32_t s, tot; unpack_header((uint8_t*)buf, t, s, tot);
                     if (t == CMD_SIZE) {
                         memcpy(&expected_size, buf + UDP_HEADER_SIZE, 8); expected_size = ntoh64(expected_size);
                         got_header = true; break;
                     }
                     if (t == CMD_DATA && s == 0) {
                         memcpy(&expected_size, buf + UDP_HEADER_SIZE, 8); expected_size = ntoh64(expected_size);
                         got_header = true; break;
                     }
                 }
            }
        }
        if (!got_header) return false;
        
        out_data.resize(expected_size);
        uint32_t total_chunks = (expected_size + UDP_PAYLOAD_SIZE - 1) / UDP_PAYLOAD_SIZE;
        std::vector<bool> received(total_chunks + 1, false);
        uint32_t received_count = 0;

        while(received_count < total_chunks) {
            fd_set rfd; FD_ZERO(&rfd); FD_SET(m_sock, &rfd); timeval tv{0, 200000}; 
            if (select(m_sock+1, &rfd, nullptr, nullptr, &tv) > 0) {
                int n = ::recvfrom(m_sock, buf, UDP_PACKET_SIZE, 0, (sockaddr*)&m_session_addr, &slen);
                if (n >= UDP_HEADER_SIZE) {
                    uint8_t t; uint32_t s, tot; unpack_header((uint8_t*)buf, t, s, tot);
                    if (t == CMD_DATA && s > 0 && s <= total_chunks) {
                         if (!received[s]) {
                             received[s] = true; received_count++;
                             memcpy(out_data.data() + (s-1)*UDP_PAYLOAD_SIZE, buf + UDP_HEADER_SIZE, n - UDP_HEADER_SIZE);
                         }
                    }
                }
            } else {
                 for(uint32_t i=1; i<=total_chunks; ++i) {
                     if(!received[i]) {
                         uint8_t nack[UDP_HEADER_SIZE + 4]; pack_header(nack, CMD_NACK, 0, 0);
                         uint32_t ns = htonl(i); memcpy(nack + UDP_HEADER_SIZE, &ns, 4);
                         ::sendto(m_sock, (char*)nack, sizeof(nack), 0, (sockaddr*)&m_session_addr, slen);
                         if (i % 20 == 0) break; 
                     }
                 }
            }
        }
        uint8_t fin[UDP_HEADER_SIZE]; pack_header(fin, CMD_DONE, 0, 0);
        for(int k=0; k<3; k++) ::sendto(m_sock, (char*)fin, sizeof(fin), 0, (sockaddr*)&m_session_addr, slen);
        return true;
    }

    bool blast_data(const uint8_t* data, size_t size) {
        uint32_t total_chunks = (size + UDP_PAYLOAD_SIZE - 1) / UDP_PAYLOAD_SIZE;
        socklen_t slen = sizeof(m_session_addr);
        for (uint32_t i = 0; i < total_chunks; ++i) {
            uint32_t off = i * UDP_PAYLOAD_SIZE;
            uint32_t len = std::min<uint32_t>(UDP_PAYLOAD_SIZE, size - off);
            uint8_t pkt[UDP_PACKET_SIZE]; pack_header(pkt, CMD_DATA, i + 1, total_chunks);
            memcpy(pkt + UDP_HEADER_SIZE, data + off, len);
            ::sendto(m_sock, (char*)pkt, UDP_HEADER_SIZE + len, 0, (sockaddr*)&m_session_addr, slen);
            if (i % 20 == 0) std::this_thread::sleep_for(std::chrono::microseconds(50));
        }

        while(true) {
            uint8_t fin[UDP_HEADER_SIZE]; pack_header(fin, CMD_DONE, 0, 0);
            ::sendto(m_sock, (char*)fin, UDP_HEADER_SIZE, 0, (sockaddr*)&m_session_addr, slen);

            char buf[UDP_PACKET_SIZE]; fd_set rfd; FD_ZERO(&rfd); FD_SET(m_sock, &rfd); timeval tv{0, 200000};
            if(select(m_sock+1, &rfd, nullptr, nullptr, &tv) > 0) {
                 int n = ::recvfrom(m_sock, buf, UDP_PACKET_SIZE, 0, (sockaddr*)&m_session_addr, &slen);
                 if (n >= UDP_HEADER_SIZE) {
                     uint8_t t; uint32_t s, tot; unpack_header((uint8_t*)buf, t, s, tot);
                     if (t == CMD_DONE) return true;
                     if (t == CMD_NACK) {
                         uint32_t seq; memcpy(&seq, buf + UDP_HEADER_SIZE, 4); seq = ntohl(seq);
                         if (seq > 0 && seq <= total_chunks) {
                             uint32_t off = (seq-1)*UDP_PAYLOAD_SIZE;
                             uint8_t pkt[UDP_PACKET_SIZE]; pack_header(pkt, CMD_DATA, seq, total_chunks);
                             memcpy(pkt + UDP_HEADER_SIZE, data + off, std::min<uint32_t>(UDP_PAYLOAD_SIZE, size - off));
                             ::sendto(m_sock, (char*)pkt, UDP_HEADER_SIZE + std::min<uint32_t>(UDP_PAYLOAD_SIZE, size - off), 0, (sockaddr*)&m_session_addr, slen);
                         }
                     }
                 }
            }
        }
    }
};

struct SyncStatus {
    enum State { Connecting, Listing, Uploading, Deleting, Done, Error };
    State state; std::string host; uint16_t port; std::string filename;
    uint64_t bytes = 0; uint64_t total = 0; std::string message;
    float percent() const { return total > 0 ? (float)bytes / total : 0.0f; }
};

class SyncClient : public ofThread
{
public:
    ofEvent<SyncStatus> syncEvent;
    fs::path m_local_root;

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_stop_requested{false};
    bool m_peers_changed = false;
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
            m_peers_changed = true; 
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

    void start() { m_stop_requested = false; if (!isThreadRunning()) startThread(); }
    void stop() { m_stop_requested = true; m_cv.notify_all(); if (isThreadRunning()) waitForThread(true); }

private:
    void notify(const std::string ip, uint16_t port, SyncStatus::State state, const std::string &msg, uint64_t bytes = 0, uint64_t total = 0, const std::string &filename = "") {
        if(m_stop_requested) return;
        SyncStatus s; s.state = state; s.host = ip; s.port = port;
        s.filename = filename.empty() ? msg : filename; s.message = msg; s.bytes = bytes; s.total = total;
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
                m_cv.wait_for(lock, std::chrono::seconds(2), [&]{ 
                    return !m_pending_paths.empty() || m_peers_changed || m_stop_requested; 
                });
                
                if (m_stop_requested) break;
                
                m_peers_changed = false; 
                local_path_updates = std::move(m_pending_paths);
                bool has_new_updates = !local_path_updates.empty();
                
                if(has_new_updates) {
                    for(auto& [key, synced] : m_peer_synced_state) synced = false;
                }

                for(const auto& [key, peer] : m_peers) {
                    if (!peer.is_self) {
                        if (!m_peer_synced_state[key]) {
                            peers_to_process.push_back({key, peer});
                        }
                    }
                }
            } 

            process_cache_updates(local_path_updates);

            bool any_success = false;
            for (const auto& item : peers_to_process) {
                if (m_stop_requested) break;
                std::string peerKey = item.first; Peer peer = item.second;
                notify(peer.ip, peer.syncPort, SyncStatus::Connecting, "Connecting...");
                
                if (!m_client.connect(peer.ip, peer.syncPort)) {
                    notify(peer.ip, peer.syncPort, SyncStatus::Error, "Socket Fail");
                    continue; 
                }

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