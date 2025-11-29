// tcp_file_transfer.hpp
// ---------------------------------------------------------------
// Single-header TCP file server + client + sync with openFrameworks
// Updated: recursive directory listing + MD5-based sync + cache invalidation
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
#include <mutex>
#include <deque>

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
#include "MD5.h" // <-- added
#define UPDATE_ME "UPDATE_ME"

namespace fs = std::filesystem;

// ------------------------------------------------------------
// Winsock init/cleanup
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
constexpr uint8_t CMD_LIST = 1;
constexpr uint8_t CMD_GET = 2;
constexpr uint8_t CMD_PUT = 3;
constexpr uint8_t CMD_DELETE = 4;
constexpr uint8_t CMD_OK = 200;
constexpr uint8_t CMD_ERR = 255;

// ------------------------------------------------------------
// Progress event
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

            ClientHandler *handler = new ClientHandler(client, m_root, this);
            handler->startThread();
        }
        closesocket(listen_sock);
    }

    void start() { startThread(); }
    void stop() { waitForThread(true); }

private:
    // Simple MD5 cache (per server instance)
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
        catch (...)
        {
        }

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

    class ClientHandler : public ofThread
    {
    public:
        ClientHandler(SOCKET sock, const fs::path &root, Server *parent)
            : m_sock(sock), m_root(root), m_parent(parent) {}

        ~ClientHandler()
        {
            if (m_sock != INVALID_SOCKET)
                closesocket(m_sock);
        }

        void threadedFunction() override
        {
            while (true)
            {
                uint8_t cmd = 0;
                if (!recv_all(m_sock, &cmd, 1))
                    break;

                if (cmd == CMD_LIST)
                {
                    list_directory_recursive(m_sock);
                }
                else if (cmd == CMD_GET)
                {
                    std::string rel;
                    if (!recv_string(m_sock, rel))
                        break;
                    send_file(m_sock, rel, m_root);
                }
                else if (cmd == CMD_PUT)
                {
                    std::string rel;
                    if (!recv_string(m_sock, rel))
                        break;
                    receive_file(m_sock, rel, m_root);
                }
                else if (cmd == CMD_DELETE)
                {
                    std::string rel;
                    if (!recv_string(m_sock, rel))
                        break;
                    delete_file(m_sock, rel, m_root);
                }
                else
                {
                    break;
                }
            }
        }

    private:
        // New recursive listing:  path|size|md5\n
        void list_directory_recursive(SOCKET sock)
        {
            std::ostringstream oss;
            for (const auto &entry : fs::recursive_directory_iterator(m_root,
                                                                      fs::directory_options::skip_permission_denied))
            {
                if (!entry.is_regular_file())
                    continue;

                fs::path rel = entry.path().lexically_relative(m_root);
                std::string rel_str = rel.string();
                uint64_t fsize = entry.file_size();
                std::string md5 = m_parent->get_cached_md5(entry.path());

                oss << rel_str << '|' << fsize << '|' << md5 << '\n';
            }

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
            {
                fs::create_directories(full.parent_path());
            }

            uint64_t fsize = 0;
            if (!recv_all(sock, &fsize, 8))
            {
                send_error(sock, "recv size failed");
                return;
            }
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
                    return; // no error send, since already sent OK
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

            // Invalidate cache after successful write
            m_parent->invalidate_md5(full.string());
        }

        void delete_file(SOCKET sock, const std::string &rel_path, const fs::path &root)
        {
            fs::path full = safe_path(rel_path, root);
            if (!fs::exists(full))
            {
                send_error(sock, "File not found");
                return;
            }
            if (!fs::is_regular_file(full))
            {
                send_error(sock, "Not a regular file");
                return;
            }

            std::error_code ec;
            fs::remove(full, ec);
            if (ec)
            {
                send_error(sock, "Delete failed: " + ec.message());
                return;
            }

            send_all(sock, &CMD_OK, 1);

            // Invalidate cache after delete
            m_parent->invalidate_md5(full.string());

            TransferProgress prog;
            prog.type = TransferProgress::Delete;
            prog.filename = rel_path;
            prog.bytesTransferred = prog.totalBytes = 1;
            ofNotifyEvent(m_parent->transferProgressEvent, prog, m_parent);
        }

        fs::path safe_path(const std::string &rel, const fs::path &root) const
        {
            fs::path p = fs::path(rel).lexically_normal();
            if (p.is_absolute())
                p = p.lexically_relative("/");
            return fs::absolute(root / p);
        }

        bool recv_string(SOCKET sock, std::string &out)
        {
            uint16_t len = 0;
            if (!recv_all(sock, &len, 2))
                return false;
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

        static uint64_t hto64(uint64_t v)
        {
            return ((v & 0xFFULL) << 56) |
                   ((v & 0xFF00ULL) << 40) |
                   ((v & 0xFF0000ULL) << 24) |
                   ((v & 0xFF000000ULL) << 8) |
                   ((v & 0xFF00000000ULL) >> 8) |
                   ((v & 0xFF0000000000ULL) >> 24) |
                   ((v & 0xFF000000000000ULL) >> 40) |
                   ((v & 0xFF00000000000000ULL) >> 56);
        }

        static uint64_t ntoh64(uint64_t v) { return hto64(v); }

        SOCKET m_sock;
        fs::path m_root;
        Server *m_parent;
    };

    uint16_t m_port = 0;
    fs::path m_root;
};

// ------------------------------------------------------------
// Client
class Client
{
public:
    Client()
    {
        if (!init_winsock())
            throw std::runtime_error("Winsock init failed");
    }

    ~Client()
    {
        disconnect();
        cleanup_winsock();
    }

    bool connect(std::string host, uint16_t port)
    {
        disconnect();
        m_sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_sock == INVALID_SOCKET)
            return false;

        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0)
            return false;

        bool ok = (::connect(m_sock, res->ai_addr, static_cast<int>(res->ai_addrlen)) != SOCKET_ERROR);
        freeaddrinfo(res);
        if (!ok)
        {
            closesocket(m_sock);
            m_sock = INVALID_SOCKET;
        }
        return ok;
    }

    void disconnect()
    {
        if (m_sock != INVALID_SOCKET)
        {
            closesocket(m_sock);
            m_sock = INVALID_SOCKET;
        }
    }

    // Returns map: relative_path -> {size, md5}
    std::unordered_map<std::string, std::pair<uint64_t, std::string>> list()
    {
        std::unordered_map<std::string, std::pair<uint64_t, std::string>> result;

        if (!send_cmd(CMD_LIST))
        {
            ofLogError("Client") << "list(): send_cmd failed";
            return result;
        }

        uint8_t status = 0;
        if (!recv_all(m_sock, &status, 1) || status != CMD_OK)
        {
            ofLogError("Client") << "list(): bad status " << (int)status;
            return result;
        }

        uint32_t sz = 0;
        if (!recv_all(m_sock, &sz, 4))
        {
            ofLogError("Client") << "list(): recv size failed";
            return result;
        }
        sz = ntohl(sz);

        if (sz == 0)
            return result;

        std::string data(sz, '\0');
        if (!recv_all(m_sock, data.data(), sz))
        {
            ofLogError("Client") << "list(): recv data failed";
            return result;
        }

        std::istringstream iss(data);
        std::string line;
        while (std::getline(iss, line))
        {
            if (line.empty())
                continue;
            size_t p1 = line.find('|');
            size_t p2 = line.rfind('|');
            if (p1 == std::string::npos || p2 == std::string::npos || p1 == p2)
                continue;

            std::string path = line.substr(0, p1);
            std::string sizestr = line.substr(p1 + 1, p2 - p1 - 1);
            std::string md5 = line.substr(p2 + 1);

            uint64_t size = 0;
            try
            {
                size = std::stoull(sizestr);
            }
            catch (...)
            {
            }

            result[path] = {size, md5};
        }
        return result;
    }

    bool download(const std::string &remote, const std::string &local)
    {
        if (!send_cmd(CMD_GET, remote))
            return false;
        uint8_t status = 0;
        if (!recv_all(m_sock, &status, 1) || status != CMD_OK)
            return false;

        uint64_t fsize = 0;
        if (!recv_all(m_sock, &fsize, 8))
            return false;
        fsize = ntoh64(fsize);

        FILE *fp = fopen(local.c_str(), "wb");
        if (!fp)
            return false;

        std::array<char, 8192> buf{};
        uint64_t remaining = fsize;
        while (remaining > 0)
        {
            size_t chunk = static_cast<size_t>(std::min<uint64_t>(remaining, buf.size()));
            if (!recv_all(m_sock, buf.data(), chunk))
            {
                fclose(fp);
                return false;
            }
            fwrite(buf.data(), 1, chunk, fp);
            remaining -= chunk;
        }
        fclose(fp);
        return true;
    }

    bool upload(const std::string &local, const std::string &remote)
    {
        if (!fs::exists(local) || !fs::is_regular_file(local))
            return false;
        if (!send_cmd(CMD_PUT, remote))
            return false;

        uint64_t fsize = fs::file_size(local);
        uint64_t net_size = hto64(fsize);
        if (!send_all(m_sock, &net_size, 8))
            return false;

        uint8_t ok = 0;
        if (!recv_all(m_sock, &ok, 1) || ok != CMD_OK)
            return false;

        FILE *fp = fopen(local.c_str(), "rb");
        if (!fp)
            return false;

        std::array<char, 8192> buf{};
        size_t read;
        while ((read = fread(buf.data(), 1, buf.size(), fp)) > 0)
        {
            if (!send_all(m_sock, buf.data(), read))
            {
                fclose(fp);
                return false;
            }
        }
        fclose(fp);
        return true;
    }

    bool remove(const std::string &remote)
    {
        if (!send_cmd(CMD_DELETE, remote))
            return false;
        uint8_t status = 0;
        if (!recv_all(m_sock, &status, 1))
            return false;
        return status == CMD_OK;
    }

private:
    bool send_cmd(uint8_t cmd, const std::string &arg = "")
    {
        if (!send_all(m_sock, &cmd, 1))
            return false;
        if (!arg.empty())
        {
            uint16_t len = htons(static_cast<uint16_t>(arg.size()));
            if (!send_all(m_sock, &len, 2))
                return false;
            if (!send_all(m_sock, arg.data(), arg.size()))
                return false;
        }
        return true;
    }

    static uint64_t hto64(uint64_t v)
    {
        return ((v & 0xFFULL) << 56) |
               ((v & 0xFF00ULL) << 40) |
               ((v & 0xFF0000ULL) << 24) |
               ((v & 0xFF000000ULL) << 8) |
               ((v & 0xFF00000000ULL) >> 8) |
               ((v & 0xFF0000000000ULL) >> 24) |
               ((v & 0xFF000000000000ULL) >> 40) |
               ((v & 0xFF00000000000000ULL) >> 56);
    }

    static uint64_t ntoh64(uint64_t v) { return hto64(v); }

    SOCKET m_sock = INVALID_SOCKET;
};

// ------------------------------------------------------------
// Sync Status
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
class SyncClient : public ofThread
{
public:
    struct Target
    {
        std::string host;
        uint16_t port;
    };
    fs::path m_local_root;

    ofEvent<SyncStatus> syncEvent;
    mutable bool canceled = false;

    mutable std::unordered_map<std::string, std::pair<uint64_t, std::string>> cache;
    std::deque<string> cacheChanges; // ← replace vector<string>
    mutable std::unordered_map<std::string, bool> peerIsSynced;
    mutable std::map<std::string, Peer> localPeers;

    ofMutex mutex;

    Client client;

    SyncClient(const std::string &local_root = ".")
        : m_local_root(fs::absolute(local_root))
    {
    }

    void setPeers(std::map<std::string, Peer> peers)
    {
        ofScopedLock lock(mutex);

        localPeers = peers;
    };

    std::map<std::string, Peer> getPeers()
    {
        ofScopedLock lock(mutex);

        return localPeers;
    }

    void stop()
    {
        canceled = true;
        if (isThreadRunning())
        {
            ofLogNotice("SyncClient") << "stopping sync client";
            stopThread();        // tells threadedFunction() to exit its loop
            waitForThread(true); // blocks until all SyncTask threads are done
        }
    }

    void start()
    {
        canceled = false;
        if (!isThreadRunning())
        {
            ofLogNotice("SyncClient") << "starting sync client";
            startThread();
        }
    }

    void pathsHasUpdated(std::vector<std::string> &paths, bool initialize = false)
    {

        if (initialize)
        {
            {
                ofScopedLock lock(mutex);
                cacheChanges.emplace_back(UPDATE_ME);
            }
        }
        for (const auto &p : paths)
        {
            {
                ofScopedLock lock(mutex);
                cacheChanges.emplace_back(p);
            }
        }
    }

    void initializeCache()
    {
        std::vector<std::string> paths;
        // Build local file map with MD5

        for (const auto &entry : fs::recursive_directory_iterator(m_local_root,
                                                                  fs::directory_options::skip_permission_denied))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }
            paths.push_back(entry.path().string());
        }
        pathsHasUpdated(paths, true);
    }

    std::unordered_map<std::string, std::pair<uint64_t, std::string>> getLocalMap()
    {
        bool done = false;
        while (!done)
        {
            string full_str;
            {
                ofScopedLock lock(mutex);
                if (cacheChanges.empty())
                {
                    done = true;
                    continue;
                }
                else
                {
                    full_str = std::move(cacheChanges.front());
                    cacheChanges.pop_front();
                }
            }

            if (full_str == UPDATE_ME)
            {
                cache.clear();
                continue;
            }

            fs::path entry = full_str;
            fs::path rel = entry.lexically_relative(m_local_root);

            std::string rel_str = rel.string();

            if (!fs::exists(entry))
            {
                cout << "file does not exist: " << rel_str << "\n";
                cache.erase(rel_str);
            }
            else
            {
                std::string hash = "";

                try
                {
                    std::ifstream f(entry, std::ios::binary);
                    if (f)
                    {
                        f.seekg(0, std::ios::end);
                        auto sz = f.tellg();
                        f.seekg(0);
                        std::string buf(sz, '\0');
                        f.read(&buf[0], sz);
                        hash = MD5::hash(buf);
                    }
                }
                catch (...)
                {
                }
                uint64_t sz = fs::file_size(entry);
                cache[rel_str] = {sz, hash};

                cout << "file exists: " << rel_str << "\n";
            }
        }

        const auto &peers = getPeers();

        for (const auto &item : peers)
        {
            peerIsSynced[item.first] = false;
        }
        return cache;
    }

    void notify(const std::string ip, const uint16_t port, SyncStatus::State state, const std::string &msg,
                uint64_t bytes = 0, uint64_t total = 0, const std::string &filename = "")
    {
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
    void threadedFunction() override
    {
        initializeCache();
        try
        {

            while (!canceled && isThreadRunning())
            {
                auto localMapCopy = getLocalMap();
                const auto &peers = getPeers();
                bool anyPeerSynced = false;

                if (true)
                {
                    for (const auto &item : peers)
                    {

                        if (canceled)
                        {
                            break;
                        }
                        bool alreadySynced = false;

                        auto it = peerIsSynced.find(item.first);
                        if (it != peerIsSynced.end())
                        {
                            alreadySynced = it->second;
                        }

                        if (item.second.is_self || alreadySynced)
                        {
                            // cout << "already synced:" << alreadySynced << " is self?: " <<   item.second.is_self  << "\n";
                            // peer is self or already synced
                            continue;
                        }

                        notify(item.second.ip, item.second.syncPort, SyncStatus::Connecting, "Connecting...");
                        if (!client.connect(item.second.ip, item.second.syncPort))
                        {
                            notify(item.second.ip, item.second.syncPort, SyncStatus::Error, "Failed to connect");
                            break;
                        }
                        notify(item.second.ip, item.second.syncPort, SyncStatus::Connecting, "Connected");

                        bool changed = true;
                        int attempts = 0;
                        const int max_attempts = 10;

                        while (!canceled && changed && attempts++ < max_attempts && isThreadRunning())
                        {
                            changed = false;

                            auto remote_map = client.list();
                            if (remote_map.empty() && !fs::exists(m_local_root))
                            {
                                notify(item.second.ip, item.second.syncPort, SyncStatus::Error, "Failed to list remote");
                                return;
                            }

                            // Upload missing or different files
                            for (const auto &local_entry : localMapCopy)
                            {
                                if (canceled)
                                    break;
                                const std::string &path = local_entry.first;
                                auto remote_it = remote_map.find(path);

                                bool need_upload = (remote_it == remote_map.end()) ||
                                                   (remote_it->second.second != local_entry.second.second);
                                cout << "file: " << local_entry.first << "\nsame: " << (remote_it->second.second != local_entry.second.second) << "\nlocal: " << local_entry.second.second << "\nremote: " << remote_it->second.second << "\n";
                                if (need_upload)
                                {
                                    std::string local_full = (m_local_root / path).string();
                                    uint64_t fsize = local_entry.second.first;
                                    notify(item.second.ip, item.second.syncPort, SyncStatus::Uploading, path, 0, fsize);
                                    if (client.upload(local_full, path))
                                    {
                                        changed = true;
                                        notify(item.second.ip, item.second.syncPort, SyncStatus::Uploading, path, fsize, fsize);
                                    }
                                    else
                                    {
                                        notify(item.second.ip, item.second.syncPort, SyncStatus::Error, "Upload failed: " + path);
                                    }
                                }
                            }

                            // Delete files that exist remotely but not locally
                            for (const auto &remote_entry : remote_map)
                            {
                                if (canceled)
                                    break;

                                const std::string &path = remote_entry.first;
                                if (localMapCopy.count(path) == 0)
                                {
                                    notify(item.second.ip, item.second.syncPort, SyncStatus::Deleting, path, 0, 1);
                                    if (client.remove(path))
                                    {
                                        changed = true;
                                        notify(item.second.ip, item.second.syncPort, SyncStatus::Deleting, path, 1, 1);
                                    }
                                    else
                                    {
                                        notify(item.second.ip, item.second.syncPort, SyncStatus::Error, "Delete failed: " + path);
                                    }
                                }
                            }

                            if (!canceled && changed)
                            {
                                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                            }
                        }
                        peerIsSynced[item.first] = attempts <= max_attempts;
                        anyPeerSynced = true;

                        notify(item.second.ip, item.second.syncPort, SyncStatus::Done, attempts <= max_attempts ? "Synced" : "Max attempts exceeded");
                    }
                    if (anyPeerSynced)
                    {
                        SyncStatus done;
                        done.state = SyncStatus::Done;
                        done.message = "All servers synchronized";
                        ofNotifyEvent(syncEvent, done, this);
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        }

        catch (const std::exception &e)
        {
            std::cerr << e.what() << '\n';
        }
    }
};

#endif // TCP_FILE_TRANSFER_HPP