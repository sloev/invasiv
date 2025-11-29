// tcp_file_transfer.hpp
// ---------------------------------------------------------------
// Single-header TCP file server + client + sync with openFrameworks
// Updated: Fixed SIGABRT on Server (detached threads)
// Updated: Fixed Sync logic (resync on file changes)
// ---------------------------------------------------------------

#ifndef TCP_FILE_TRANSFER_HPP
#define TCP_FILE_TRANSFER_HPP

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
// Winsock init/cleanup
// ------------------------------------------------------------
inline bool init_winsock()
{
#ifdef _WIN32
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
    return true;
#endif
}
inline void cleanup_winsock()
{
#ifdef _WIN32
    WSACleanup();
#endif
}

// ------------------------------------------------------------
// Network helpers
// ------------------------------------------------------------
inline bool send_all(SOCKET s, const void *data, std::size_t len)
{
    const char *ptr = static_cast<const char *>(data);
    while (len > 0)
    {
        int sent = ::send(s, ptr, static_cast<int>(len), 0);
        if (sent == SOCKET_ERROR)
            return false;
        ptr += sent;
        len -= sent;
    }
    return true;
}

inline bool recv_all(SOCKET s, void *data, std::size_t len)
{
    char *ptr = static_cast<char *>(data);
    while (len > 0)
    {
        int rcvd = ::recv(s, ptr, static_cast<int>(len), 0);
        if (rcvd <= 0)
            return false;
        ptr += rcvd;
        len -= rcvd;
    }
    return true;
}

// ------------------------------------------------------------
// Protocol
// ------------------------------------------------------------
constexpr uint8_t CMD_LIST = 1;
constexpr uint8_t CMD_GET = 2;
constexpr uint8_t CMD_PUT = 3;
constexpr uint8_t CMD_DELETE = 4;
constexpr uint8_t CMD_OK = 200;
constexpr uint8_t CMD_ERR = 255;

// ------------------------------------------------------------
// Progress event
// ------------------------------------------------------------
struct TransferProgress
{
    enum Type
    {
        Upload,
        Download,
        Delete
    };
    Type type;
    std::string filename;
    uint64_t bytesTransferred = 0;
    uint64_t totalBytes = 0;
    float percent() const { return totalBytes > 0 ? (float)bytesTransferred / totalBytes : 1.0f; }
};

// ------------------------------------------------------------
// Server
// ------------------------------------------------------------
class Server : public ofThread
{
public:
    ofEvent<TransferProgress> transferProgressEvent;

    Server(uint16_t port, const std::string &root_dir = ".")
        : m_port(port), m_root(fs::absolute(root_dir))
    {
        if (!init_winsock())
            throw std::runtime_error("Winsock init failed");
    }

    ~Server()
    {
        stopThread();
        cleanup_winsock();
    }

    void threadedFunction() override
    {
        SOCKET listen_sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listen_sock == INVALID_SOCKET)
        {
            ofLogError("tcp_server") << "socket() failed";
            return;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(m_port);
        addr.sin_addr.s_addr = INADDR_ANY;

        if (::bind(listen_sock, (sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR)
        {
            closesocket(listen_sock);
            ofLogError("tcp_server") << "bind() failed";
            return;
        }

        if (::listen(listen_sock, SOMAXCONN) == SOCKET_ERROR)
        {
            closesocket(listen_sock);
            ofLogError("tcp_server") << "listen() failed";
            return;
        }

        ofLogNotice("tcp_server") << "Listening on port " << m_port << " (root: " << m_root << ")";

        while (isThreadRunning())
        {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            
            SOCKET client = ::accept(listen_sock, (sockaddr *)&client_addr, &client_len);
            if (client == INVALID_SOCKET)
            {
                if (isThreadRunning())
                    ofLogError("tcp_server") << "accept() error";
                continue;
            }

            // FIX: Using dedicated detached thread wrapper instead of inheriting ofThread
            ClientHandler *handler = new ClientHandler(client, m_root, this);
            handler->start(); 
        }
        closesocket(listen_sock);
    }

    void start() { startThread(); }
    void stop() { waitForThread(true); }

private:
    mutable std::mutex m_cache_mutex;
    mutable std::unordered_map<std::string, std::string> m_md5_cache;

    std::string get_cached_md5(const fs::path &full_path) const
    {
        std::string key = full_path.string();
        {
            std::lock_guard<std::mutex> tryLock(m_cache_mutex);
            auto it = m_md5_cache.find(key);
            if (it != m_md5_cache.end())
                return it->second;
        }

        std::string hash;
        try
        {
            std::ifstream file(full_path, std::ios::binary);
            if (file)
            {
                file.seekg(0, std::ios::end);
                size_t sz = file.tellg();
                file.seekg(0);
                std::string data(sz, '\0');
                file.read(&data[0], sz);
                hash = MD5::hash(data);
            }
        }
        catch (...) {}

        {
            std::lock_guard<std::mutex> tryLock(m_cache_mutex);
            m_md5_cache[key] = hash;
        }
        return hash;
    }

    void invalidate_md5(const std::string &key)
    {
        std::lock_guard<std::mutex> tryLock(m_cache_mutex);
        m_md5_cache.erase(key);
    }
    
    // Grant access to ClientHandler
    friend class ClientHandler;

    // FIX: Removed "public ofThread" inheritance to prevent SIGABRT on "delete this"
    class ClientHandler 
    {
    public:
        ClientHandler(SOCKET sock, const fs::path &root, Server *parent)
            : m_sock(sock), m_root(root), m_parent(parent) {}

        ~ClientHandler()
        {
            if (m_sock != INVALID_SOCKET)
                closesocket(m_sock);
        }

        // Helper to launch detached thread
        void start() 
        {
            std::thread t(&ClientHandler::run, this);
            t.detach();
        }

        // Renamed from threadedFunction to run
        void run()
        {
            while (true)
            {
                uint8_t cmd = 0;
                if (!recv_all(m_sock, &cmd, 1))
                    break;

                if (cmd == CMD_LIST)      list_directory_recursive(m_sock);
                else if (cmd == CMD_GET) {
                    std::string rel;
                    if (recv_string(m_sock, rel)) send_file(m_sock, rel, m_root);
                    else break;
                }
                else if (cmd == CMD_PUT) {
                    std::string rel;
                    if (recv_string(m_sock, rel)) receive_file(m_sock, rel, m_root);
                    else break;
                }
                else if (cmd == CMD_DELETE) {
                    std::string rel;
                    if (recv_string(m_sock, rel)) delete_file(m_sock, rel, m_root);
                    else break;
                }
                else break;
            }
            // FIX: Self-delete is now safe because the thread is detached
            delete this; 
        }

    private:
        void list_directory_recursive(SOCKET sock)
        {
            std::ostringstream oss;
            try {
                if(fs::exists(m_root)) {
                     for (const auto &entry : fs::recursive_directory_iterator(m_root, fs::directory_options::skip_permission_denied))
                    {
                        if (!entry.is_regular_file()) continue;
                        fs::path rel = entry.path().lexically_relative(m_root);
                        std::string rel_str = rel.string();
                        // Use "/" for network protocol consistency
                        std::replace(rel_str.begin(), rel_str.end(), '\\', '/'); 
                        uint64_t fsize = entry.file_size();
                        std::string md5 = m_parent->get_cached_md5(entry.path());
                        oss << rel_str << '|' << fsize << '|' << md5 << '\n';
                    }
                }
            } catch(...) {}

            std::string data = oss.str();
            uint32_t sz = htonl(static_cast<uint32_t>(data.size()));
            send_all(sock, &CMD_OK, 1);
            send_all(sock, &sz, 4);
            send_all(sock, data.data(), data.size());
        }

        void send_file(SOCKET sock, const std::string &rel_path, const fs::path &root)
        {
            fs::path full = safe_path(rel_path, root);
            if (!fs::exists(full) || !fs::is_regular_file(full))
            {
                send_error(sock, "File not found");
                return;
            }

            uint64_t fsize = fs::file_size(full);
            uint64_t net_size = hto64(fsize);
            send_all(sock, &CMD_OK, 1);
            send_all(sock, &net_size, 8);

            FILE *fp = fopen(full.string().c_str(), "rb");
            if (!fp)
            {
                send_error(sock, "Cannot open file");
                return;
            }

            std::array<char, 8192> buf{};
            size_t read;
            uint64_t transferred = 0;
            const uint64_t reportInterval = 65536;

            while ((read = fread(buf.data(), 1, buf.size(), fp)) > 0)
            {
                if (!send_all(sock, buf.data(), read))
                    break;
                transferred += read;
                if ((transferred % reportInterval < read) || transferred == fsize)
                {
                    TransferProgress prog;
                    prog.type = TransferProgress::Download;
                    prog.filename = rel_path;
                    prog.bytesTransferred = transferred;
                    prog.totalBytes = fsize;
                    ofNotifyEvent(m_parent->transferProgressEvent, prog, m_parent);
                }
            }
            fclose(fp);
        }

        void receive_file(SOCKET sock, const std::string &rel_path, const fs::path &root)
        {
            fs::path full = safe_path(rel_path, root);
            if (full.has_parent_path() && !fs::exists(full.parent_path()))
                fs::create_directories(full.parent_path());

            uint64_t fsize = 0;
            if (!recv_all(sock, &fsize, 8)) return;
            fsize = ntoh64(fsize);

            FILE *fp = fopen(full.string().c_str(), "wb");
            if (!fp)
            {
                send_error(sock, "Cannot create file");
                return;
            }

            send_all(sock, &CMD_OK, 1);

            std::array<char, 8192> buf{};
            uint64_t transferred = 0;
            const uint64_t reportInterval = 65536;

            while (transferred < fsize)
            {
                size_t to_read = static_cast<size_t>(std::min<uint64_t>(fsize - transferred, buf.size()));
                if (!recv_all(sock, buf.data(), to_read))
                {
                    fclose(fp);
                    return;
                }
                fwrite(buf.data(), 1, to_read, fp);
                transferred += to_read;

                if ((transferred % reportInterval < to_read) || transferred == fsize)
                {
                    TransferProgress prog;
                    prog.type = TransferProgress::Upload;
                    prog.filename = rel_path;
                    prog.bytesTransferred = transferred;
                    prog.totalBytes = fsize;
                    ofNotifyEvent(m_parent->transferProgressEvent, prog, m_parent);
                }
            }
            fclose(fp);
            m_parent->invalidate_md5(full.string());
        }

        void delete_file(SOCKET sock, const std::string &rel_path, const fs::path &root)
        {
            fs::path full = safe_path(rel_path, root);
            if (!fs::exists(full)) { send_error(sock, "File not found"); return; }
            
            std::error_code ec;
            fs::remove(full, ec);
            if (ec) { send_error(sock, "Delete failed"); return; }

            send_all(sock, &CMD_OK, 1);
            m_parent->invalidate_md5(full.string());

            TransferProgress prog;
            prog.type = TransferProgress::Delete;
            prog.filename = rel_path;
            prog.bytesTransferred = 1; prog.totalBytes = 1;
            ofNotifyEvent(m_parent->transferProgressEvent, prog, m_parent);
        }

        fs::path safe_path(const std::string &rel, const fs::path &root) const
        {
            fs::path p = fs::path(rel).lexically_normal();
            if (p.is_absolute()) p = p.lexically_relative("/");
            return fs::absolute(root / p);
        }

        bool recv_string(SOCKET sock, std::string &out)
        {
            uint16_t len = 0;
            if (!recv_all(sock, &len, 2)) return false;
            len = ntohs(len);
            out.resize(len);
            return recv_all(sock, out.data(), len);
        }

        void send_error(SOCKET sock, const std::string &msg)
        {
            uint16_t len = htons(static_cast<uint16_t>(msg.size()));
            send_all(sock, &CMD_ERR, 1);
            send_all(sock, &len, 2);
            send_all(sock, msg.data(), msg.size());
        }

        static uint64_t hto64(uint64_t v) { return ((v & 0xFFULL) << 56) | ((v & 0xFF00ULL) << 40) | ((v & 0xFF0000ULL) << 24) | ((v & 0xFF000000ULL) << 8) | ((v & 0xFF00000000ULL) >> 8) | ((v & 0xFF0000000000ULL) >> 24) | ((v & 0xFF000000000000ULL) >> 40) | ((v & 0xFF00000000000000ULL) >> 56); }
        static uint64_t ntoh64(uint64_t v) { return hto64(v); }

        SOCKET m_sock;
        fs::path m_root;
        Server *m_parent;
    };

    uint16_t m_port = 0;
    fs::path m_root;
};

// ------------------------------------------------------------
// Client (Synchronous helper)
// ------------------------------------------------------------
class Client
{
public:
    Client() { init_winsock(); }
    ~Client() { disconnect(); cleanup_winsock(); }

    bool connect(std::string host, uint16_t port)
    {
        disconnect();
        m_sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_sock == INVALID_SOCKET) return false;

        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0) return false;
        
        // Simple blocking connect with system timeout
        bool ok = (::connect(m_sock, res->ai_addr, static_cast<int>(res->ai_addrlen)) != SOCKET_ERROR);
        freeaddrinfo(res);
        if (!ok) { closesocket(m_sock); m_sock = INVALID_SOCKET; }
        return ok;
    }

    void disconnect()
    {
        if (m_sock != INVALID_SOCKET) { closesocket(m_sock); m_sock = INVALID_SOCKET; }
    }

    std::unordered_map<std::string, std::pair<uint64_t, std::string>> list()
    {
        std::unordered_map<std::string, std::pair<uint64_t, std::string>> result;
        if (!send_cmd(CMD_LIST)) return result;

        uint8_t status = 0;
        if (!recv_all(m_sock, &status, 1) || status != CMD_OK) return result;

        uint32_t sz = 0;
        if (!recv_all(m_sock, &sz, 4)) return result;
        sz = ntohl(sz);
        if (sz == 0) return result;

        std::string data(sz, '\0');
        if (!recv_all(m_sock, data.data(), sz)) return result;

        std::istringstream iss(data);
        std::string line;
        while (std::getline(iss, line))
        {
            if (line.empty()) continue;
            size_t p1 = line.find('|');
            size_t p2 = line.rfind('|');
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

    bool download(const std::string &remote, const std::string &local)
    {
        if (!send_cmd(CMD_GET, remote)) return false;
        uint8_t status = 0;
        if (!recv_all(m_sock, &status, 1) || status != CMD_OK) return false;

        uint64_t fsize = 0;
        if (!recv_all(m_sock, &fsize, 8)) return false;
        fsize = ntoh64(fsize);

        FILE *fp = fopen(local.c_str(), "wb");
        if (!fp) return false;

        std::array<char, 8192> buf{};
        uint64_t remaining = fsize;
        while (remaining > 0)
        {
            size_t chunk = static_cast<size_t>(std::min<uint64_t>(remaining, buf.size()));
            if (!recv_all(m_sock, buf.data(), chunk)) { fclose(fp); return false; }
            fwrite(buf.data(), 1, chunk, fp);
            remaining -= chunk;
        }
        fclose(fp);
        return true;
    }

    bool upload(const std::string &local, const std::string &remote)
    {
        if (!fs::exists(local) || !fs::is_regular_file(local)) return false;
        if (!send_cmd(CMD_PUT, remote)) return false;

        uint64_t fsize = fs::file_size(local);
        uint64_t net_size = hto64(fsize);
        if (!send_all(m_sock, &net_size, 8)) return false;

        uint8_t ok = 0;
        if (!recv_all(m_sock, &ok, 1) || ok != CMD_OK) return false;

        FILE *fp = fopen(local.c_str(), "rb");
        if (!fp) return false;

        std::array<char, 8192> buf{};
        size_t read;
        while ((read = fread(buf.data(), 1, buf.size(), fp)) > 0)
        {
            if (!send_all(m_sock, buf.data(), read)) { fclose(fp); return false; }
        }
        fclose(fp);
        return true;
    }

    bool remove(const std::string &remote)
    {
        if (!send_cmd(CMD_DELETE, remote)) return false;
        uint8_t status = 0;
        if (!recv_all(m_sock, &status, 1)) return false;
        return status == CMD_OK;
    }

private:
    bool send_cmd(uint8_t cmd, const std::string &arg = "")
    {
        if (!send_all(m_sock, &cmd, 1)) return false;
        if (!arg.empty())
        {
            uint16_t len = htons(static_cast<uint16_t>(arg.size()));
            if (!send_all(m_sock, &len, 2)) return false;
            if (!send_all(m_sock, arg.data(), arg.size())) return false;
        }
        return true;
    }
    static uint64_t hto64(uint64_t v) { return ((v & 0xFFULL) << 56) | ((v & 0xFF00ULL) << 40) | ((v & 0xFF0000ULL) << 24) | ((v & 0xFF000000ULL) << 8) | ((v & 0xFF00000000ULL) >> 8) | ((v & 0xFF0000000000ULL) >> 24) | ((v & 0xFF000000000000ULL) >> 40) | ((v & 0xFF00000000000000ULL) >> 56); }
    static uint64_t ntoh64(uint64_t v) { return hto64(v); }

    SOCKET m_sock = INVALID_SOCKET;
};

// ------------------------------------------------------------
// Sync Status
// ------------------------------------------------------------
struct SyncStatus
{
    enum State
    {
        Connecting,
        Listing,
        Uploading,
        Deleting,
        Done,
        Error
    };
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
    // Thread safety primitives
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_stop_requested{false};

    // Shared Data (Protected by m_mutex)
    std::map<std::string, Peer> m_peers;
    std::map<std::string, bool> m_peer_synced_state;
    std::deque<std::string> m_pending_paths;
    
    // Thread-Local Data (No mutex needed)
    // The worker thread maintains its own map to avoid locking the mutex during IO/Hashing
    std::unordered_map<std::string, std::pair<uint64_t, std::string>> m_local_cache;

    Client m_client; // synchronous client used only by the thread

public:
    SyncClient(const std::string &local_root = ".")
        : m_local_root(fs::absolute(local_root))
    {
    }

    ~SyncClient()
    {
        stop();
    }

    // THREAD-SAFE: Can be called from any thread
    void setPeers(const std::map<std::string, Peer>& peers)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_peers = peers;
            
            // Reconcile synced state: Remove old peers, add new ones as unsynced
            // This prevents the "segfault" where the thread tries to access a deleted peer key
            std::map<std::string, bool> new_synced_state;
            for(auto const& [key, peer] : m_peers) {
                if (m_peer_synced_state.count(key)) {
                    new_synced_state[key] = m_peer_synced_state[key]; // keep state
                } else {
                    new_synced_state[key] = false; // new peer
                }
            }
            m_peer_synced_state = new_synced_state;
        }
        // Wake up thread to process new peers immediately
        m_cv.notify_one(); 
    }

    // THREAD-SAFE: Can be called from any thread
    std::map<std::string, Peer> getPeers()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_peers;
    }

    // THREAD-SAFE: Can be called from any thread
    void pathsHasUpdated(const std::vector<std::string> &paths, bool initialize = false)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (initialize) {
                m_pending_paths.push_back(UPDATE_ME);
            }
            for (const auto &p : paths) {
                m_pending_paths.push_back(p);
            }
        }
        m_cv.notify_one();
    }

    void start()
    {
        m_stop_requested = false;
        if (!isThreadRunning()) {
            ofLogNotice("SyncClient") << "Starting sync client";
            startThread();
        }
    }

    void stop()
    {
        m_stop_requested = true;
        m_cv.notify_all(); // Wake up thread if sleeping
        if (isThreadRunning()) {
            ofLogNotice("SyncClient") << "Stopping sync client";
            // Do not call m_client.disconnect() here, it causes race if thread is using it.
            // rely on thread checking m_stop_requested.
            waitForThread(true);
        }
    }

private:
    // Helper to send events back to main thread safely
    void notify(const std::string ip, uint16_t port, SyncStatus::State state, const std::string &msg,
                uint64_t bytes = 0, uint64_t total = 0, const std::string &filename = "")
    {
        if(m_stop_requested) return;
        SyncStatus s;
        s.state = state;
        s.host = ip;
        s.port = port;
        s.filename = filename.empty() ? msg : filename;
        s.message = msg;
        s.bytes = bytes;
        s.total = total;
        ofNotifyEvent(syncEvent, s, this);
    }

    // Update local MD5 cache based on pending paths
    // Returns true if cache was fully rebuilt
    void process_cache_updates(std::deque<std::string>& updates) 
    {
        while(!updates.empty()) {
            if(m_stop_requested) return;

            std::string path_str = updates.front();
            updates.pop_front();

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
            // normalize path separators
            std::replace(rel_str.begin(), rel_str.end(), '\\', '/'); 

            if (!fs::exists(entry)) {
                m_local_cache.erase(rel_str);
            } else {
                // Calculate MD5
                std::string hash = "";
                try {
                    std::ifstream f(entry, std::ios::binary);
                    if (f) {
                        f.seekg(0, std::ios::end);
                        auto sz = f.tellg();
                        f.seekg(0);
                        std::string buf(sz, '\0');
                        f.read(&buf[0], sz);
                        hash = MD5::hash(buf);
                    }
                } catch (...) {}
                
                uint64_t sz = 0;
                try { sz = fs::file_size(entry); } catch(...) {}
                m_local_cache[rel_str] = {sz, hash};
            }
        }
    }

    void threadedFunction() override
    {
        // Initial scan request
        pathsHasUpdated({}, true);

        while (!m_stop_requested)
        {
            // -------------------------------------------------------
            // SNAPSHOT PHASE
            // -------------------------------------------------------
            // We lock mutex only to copy what we need to do.
            // This prevents race conditions with setPeers/pathsHasUpdated
            
            std::deque<std::string> local_path_updates;
            std::vector<std::pair<std::string, Peer>> peers_to_process;

            {
                std::unique_lock<std::mutex> lock(m_mutex);
                
                // Wait until we have work OR a timeout (heartbeat)
                // We timeout every 2 seconds to retry failed peers or check connection health
                m_cv.wait_for(lock, std::chrono::seconds(1), [&]{ 
                    return !m_pending_paths.empty() || m_stop_requested; 
                });

                if (m_stop_requested) break;

                // Move pending paths to local queue
                local_path_updates = std::move(m_pending_paths);
                bool has_new_updates = !local_path_updates.empty();

                // Snapshot peers that need syncing
                for(const auto& [key, peer] : m_peers) {
                    bool is_synced = m_peer_synced_state[key];
                    // FIX: If we have new updates, we must sync everyone, regardless of previous state.
                    if (!peer.is_self && (!is_synced || has_new_updates)) {
                        peers_to_process.push_back({key, peer});
                        
                        // Mark as unsynced now so we track the success of THIS new cycle
                        if (has_new_updates) {
                            m_peer_synced_state[key] = false;
                        }
                    }
                }
            } 
            // Lock released. We can now do heavy IO and Network without blocking setPeers.

            // -------------------------------------------------------
            // PROCESSING PHASE
            // -------------------------------------------------------
            
            // 1. Update Cache (Heavy Disk IO)
            process_cache_updates(local_path_updates);

            // 2. Sync Peers (Heavy Network IO)
            bool any_success = false;
            
            for (const auto& item : peers_to_process)
            {
                if (m_stop_requested) break;

                std::string peerKey = item.first;
                Peer peer = item.second;

                notify(peer.ip, peer.syncPort, SyncStatus::Connecting, "Connecting...");
                
                if (!m_client.connect(peer.ip, peer.syncPort)) {
                    notify(peer.ip, peer.syncPort, SyncStatus::Error, "Failed to connect");
                    continue; // Try next peer
                }

                notify(peer.ip, peer.syncPort, SyncStatus::Connecting, "Connected");

                bool success = false;
                int attempts = 0;
                const int max_attempts = 5; 

                // Sync Loop for this peer
                while(attempts < max_attempts && !m_stop_requested)
                {
                    attempts++;
                    bool changed = false;

                    auto remote_map = m_client.list();
                    if (remote_map.empty() && !fs::exists(m_local_root)) {
                         // Remote list failed or empty, treating as failure for safety if local has files
                         if(!m_local_cache.empty()) {
                             notify(peer.ip, peer.syncPort, SyncStatus::Error, "Failed to list remote");
                             break;
                         }
                    }

                    // A. Upload missing/changed
                    for (const auto &local_entry : m_local_cache)
                    {
                        if (m_stop_requested) break;
                        const std::string &path = local_entry.first;
                        auto remote_it = remote_map.find(path);

                        bool need_upload = (remote_it == remote_map.end()) ||
                                           (remote_it->second.second != local_entry.second.second); // Compare MD5

                        if (need_upload)
                        {
                            std::string local_full = (m_local_root / path).string();
                            uint64_t fsize = local_entry.second.first;
                            
                            notify(peer.ip, peer.syncPort, SyncStatus::Uploading, path, 0, fsize);
                            if (m_client.upload(local_full, path)) {
                                changed = true;
                                notify(peer.ip, peer.syncPort, SyncStatus::Uploading, path, fsize, fsize);
                            } else {
                                notify(peer.ip, peer.syncPort, SyncStatus::Error, "Upload failed: " + path);
                            }
                        }
                    }

                    // B. Delete remote files not in local
                    for (const auto &remote_entry : remote_map)
                    {
                        if (m_stop_requested) break;
                        const std::string &path = remote_entry.first;
                        
                        // If we don't have it locally, delete it remotely
                        if (m_local_cache.find(path) == m_local_cache.end())
                        {
                            notify(peer.ip, peer.syncPort, SyncStatus::Deleting, path, 0, 1);
                            if (m_client.remove(path)) {
                                changed = true;
                                notify(peer.ip, peer.syncPort, SyncStatus::Deleting, path, 1, 1);
                            } else {
                                notify(peer.ip, peer.syncPort, SyncStatus::Error, "Delete failed: " + path);
                            }
                        }
                    }

                    if (!changed) {
                        success = true; // Synced!
                        break;
                    }
                }
                
                m_client.disconnect();

                if (success) {
                    notify(peer.ip, peer.syncPort, SyncStatus::Done, "Synced");
                    any_success = true;
                    
                    // Update state to avoid re-syncing this peer immediately
                    std::lock_guard<std::mutex> lock(m_mutex);
                    // Check if peer still exists (setPeers might have run)
                    if(m_peer_synced_state.count(peerKey)) {
                        m_peer_synced_state[peerKey] = true;
                    }
                } else {
                     notify(peer.ip, peer.syncPort, SyncStatus::Done, "Sync Incomplete");
                }
            }

            if (any_success && !m_stop_requested) {
                 SyncStatus done; 
                 done.state = SyncStatus::Done; 
                 done.message = "Synchronization Cycle Complete"; 
                 ofNotifyEvent(syncEvent, done, this);
            }
        }
    }
};

#endif // TCP_FILE_TRANSFER_HPP