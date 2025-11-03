// tcp_file_transfer.hpp
// ---------------------------------------------------------------
// Minimal single-header TCP file server + client (C++17)
// ---------------------------------------------------------------
// Features
//   • Server: list directory, send file, receive file
//   • Client: list remote dir, download file, upload file
//   • Cross-platform (Windows / POSIX)
//   • No external dependencies (uses only <winsock2.h> on Windows)
//   • Integrated with openFrameworks ofThread for multi-threading
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
#include <atomic>

// openFrameworks integration (ofThread from ofThread.h/cpp)
// ----------------------------------------------------------
// Paste the full ofThread.h content here if needed, but assuming OF is installed.
// For standalone, include the ofThread implementation below.

// Minimal ofThread implementation extracted/adapted from openFrameworks
// (Based on https://github.com/openframeworks/openFrameworks/blob/master/libs/openFrameworks/utils/ofThread.cpp)
// This is a simplified version for this header; full OF has more features.

#include <ofThread.h>

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

namespace tcp_file
{

    namespace fs = std::filesystem;

    // ---------------------------------------------------------------------
    // Helper: initialize Winsock (Windows only)
    inline bool init_winsock()
    {
#ifdef _WIN32
        WSADATA wsa;
        return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
        return true;
#endif
    }

    // ---------------------------------------------------------------------
    // Helper: clean up Winsock
    inline void cleanup_winsock()
    {
#ifdef _WIN32
        WSACleanup();
#endif
    }

    // ---------------------------------------------------------------------
    // Helper: send exactly N bytes
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

    // ---------------------------------------------------------------------
    // Helper: receive exactly N bytes
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

    // ---------------------------------------------------------------------
    // Protocol constants
    constexpr uint8_t CMD_LIST = 1;
    constexpr uint8_t CMD_GET = 2;
    constexpr uint8_t CMD_PUT = 3;
    constexpr uint8_t CMD_DELETE = 4; // NEW
    constexpr uint8_t CMD_OK = 200;
    constexpr uint8_t CMD_ERR = 255;
    // ------------------------------------------------------------
    // Add after Client class (inside namespace tcp_file)
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

    // ---------------------------------------------------------------------
    // Server implementation using ofThread
    class Server : public ofThread
    {
    public:
        explicit Server(uint16_t port, const std::string &root_dir = ".")
            : m_port(port), m_root(fs::canonical(root_dir))
        {
            if (!init_winsock())
            {
                throw std::runtime_error("Failed to initialize Winsock");
            }
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
                ofLogError("tcp_file_server") << "socket() failed";
                return;
            }

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(m_port);
            addr.sin_addr.s_addr = INADDR_ANY;

            if (::bind(listen_sock, (sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR)
            {
                closesocket(listen_sock);
                ofLogError("tcp_file_server") << "bind() failed";
                return;
            }

            if (::listen(listen_sock, SOMAXCONN) == SOCKET_ERROR)
            {
                closesocket(listen_sock);
                ofLogError("tcp_file_server") << "listen() failed";
                return;
            }

            ofLogNotice("tcp_file_server") << "Server listening on port " << m_port
                                           << " (root: " << m_root << ")";

            while (isThreadRunning())
            {
                sockaddr_in client_addr{};
                socklen_t client_len = sizeof(client_addr);
                SOCKET client = ::accept(listen_sock, (sockaddr *)&client_addr, &client_len);
                if (client == INVALID_SOCKET)
                {
                    if (isThreadRunning())
                        ofLogError("tcp_file_server") << "accept() error";
                    continue;
                }
                // Spawn new thread for each client using ofThread
                ClientHandler *handler = new ClientHandler(client, m_root);
                handler->startThread(); // no arguments
            }
            closesocket(listen_sock);
        }

        void start()
        {
            startThread();
        }

        void stop()
        {
            stopThread();
        }

    private:
        // Client handler as separate ofThread
        class ClientHandler : public ofThread
        {
        public:
            ClientHandler(SOCKET sock, const fs::path &root) : m_sock(sock), m_root(root) {}

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
                        list_directory(m_sock);
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
                    // Inside ClientHandler::threadedFunction() loop
                    else if (cmd == CMD_DELETE)
                    {
                        std::string rel;
                        if (!recv_string(m_sock, rel))
                            break;
                        delete_file(m_sock, rel, m_root, m_parent);
                    }
                    else
                    {
                        break;
                    }
                }
                delete this;
            }

        private:
            void list_directory(SOCKET sock)
            {
                std::ostringstream oss;
                for (const auto &entry : fs::directory_iterator(m_root))
                {
                    oss << (entry.is_directory() ? "d" : "-")
                        << entry.path().filename().string() << '\n';
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
                while ((read = fread(buf.data(), 1, buf.size(), fp)) > 0)
                {
                    if (!send_all(sock, buf.data(), read))
                        break;
                }
                fclose(fp);
            }
            // New method in ClientHandler
            void delete_file(SOCKET sock, const std::string &rel_path, const fs::path &root, Server *parent)
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
                uint64_t remaining = fsize;
                while (remaining > 0)
                {
                    size_t to_read = static_cast<size_t>(std::min<uint64_t>(remaining, buf.size()));
                    if (!recv_all(sock, buf.data(), to_read))
                        break;
                    fwrite(buf.data(), 1, to_read, fp);
                    remaining -= to_read;
                }
                fclose(fp);
            }

            fs::path safe_path(const std::string &rel, const fs::path &root) const
            {
                fs::path p = fs::path(rel).lexically_normal();
                if (p.is_absolute())
                    p = p.lexically_relative("/");
                return fs::canonical(root / p);
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
        };

        uint16_t m_port = 0;
        fs::path m_root;
    };

    // ---------------------------------------------------------------------
    // Client implementation (non-threaded, but can be used in OF threads)
    class Client
    {
    public:
        Client(const std::string &host, uint16_t port) : m_host(host), m_port(port)
        {
            if (!init_winsock())
                throw std::runtime_error("Winsock init failed");
        }

        ~Client()
        {
            disconnect();
            cleanup_winsock();
        }

        // In Client class (public)
        bool remove(const std::string &remote)
        {
            if (!send_cmd(CMD_DELETE, remote))
                return false;

            uint8_t status = 0;
            if (!recv_all(m_sock, &status, 1))
                return false;
            return status == CMD_OK;
        }

        bool connect()
        {
            disconnect();

            m_sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (m_sock == INVALID_SOCKET)
                return false;

            addrinfo hints{}, *res = nullptr;
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;

            if (getaddrinfo(m_host.c_str(), std::to_string(m_port).c_str(), &hints, &res) != 0)
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

        // ----- LIST -----
        std::string list()
        {
            if (!send_cmd(CMD_LIST))
                return "";
            uint8_t status = 0;
            if (!recv_all(m_sock, &status, 1) || status != CMD_OK)
                return "";
            uint32_t sz = 0;
            if (!recv_all(m_sock, &sz, 4))
                return "";
            sz = ntohl(sz);
            std::string data(sz, '\0');
            if (!recv_all(m_sock, data.data(), sz))
                return "";
            return data;
        }

        // ----- DOWNLOAD -----
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

        // ----- UPLOAD -----
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

        std::string m_host;
        uint16_t m_port;
        SOCKET m_sock = INVALID_SOCKET;
    };

    // Threaded Sync Client
    class SyncClient : public ofThread
    {
    public:
        struct Target
        {
            std::string host;
            uint16_t port;
        };

        ofEvent<SyncStatus> syncEvent;

        SyncClient(const std::vector<Target> &targets, const std::string &local_root = ".")
            : m_targets(targets), m_local_root(fs::canonical(local_root)) {}

        void threadedFunction() override
        {
            std::vector<std::unique_ptr<SyncTask>> tasks;
            for (const auto &t : m_targets)
            {
                auto task = std::make_unique<SyncTask>(t.host, t.port, m_local_root, this);
                task->startThread();
                tasks.push_back(std::move(task));
            }

            // Wait for all
            for (auto &t : tasks)
            {
                t->waitForThread();
            }

            SyncStatus done;
            done.state = SyncStatus::Done;
            done.message = "All servers synchronized";
            ofNotifyEvent(syncEvent, done, this);
        }

    private:
        class SyncTask : public ofThread
        {
        public:
            SyncTask(const std::string &host, uint16_t port, const fs::path &local_root, SyncClient *parent)
                : m_host(host), m_port(port), m_local_root(local_root), m_parent(parent) {}

            void threadedFunction() override
            {
                Client client(m_host, m_port);
                notify(SyncStatus::Connecting, "Connecting...");

                if (!client.connect())
                {
                    notify(SyncStatus::Error, "Failed to connect");
                    return;
                }
                notify(SyncStatus::Connecting, "Connected");

                // Get local file set
                std::set<std::string> local_files;
                for (const auto &entry : fs::directory_iterator(m_local_root))
                {
                    if (entry.is_regular_file())
                    {
                        local_files.insert(entry.path().filename().string());
                    }
                }

                bool changed = true;
                int attempts = 0;
                const int max_attempts = 10;

                while (changed && attempts++ < max_attempts && isThreadRunning())
                {
                    changed = false;

                    // Get remote list
                    std::string listing = client.list();
                    if (listing.empty())
                    {
                        notify(SyncStatus::Error, "Failed to list remote");
                        return;
                    }

                    std::set<std::string> remote_files;
                    std::istringstream iss(listing);
                    std::string line;
                    while (std::getline(iss, line))
                    {
                        if (line.empty())
                            continue;
                        char type = line[0];
                        std::string name = line.substr(1);
                        if (type == '-')
                            remote_files.insert(name);
                    }

                    // Upload missing
                    for (const auto &f : local_files)
                    {
                        if (remote_files.count(f) == 0)
                        {
                            std::string local_path = (m_local_root / f).string();
                            notify(SyncStatus::Uploading, f, 0, 0);
                            if (client.upload(local_path, f))
                            {
                                changed = true;
                                notify(SyncStatus::Uploading, f, 1, 1);
                            }
                            else
                            {
                                notify(SyncStatus::Error, "Upload failed: " + f);
                            }
                        }
                    }

                    // Delete extra
                    for (const auto &f : remote_files)
                    {
                        if (local_files.count(f) == 0)
                        {
                            notify(SyncStatus::Deleting, f);
                            if (client.remove(f))
                            {
                                changed = true;
                            }
                            else
                            {
                                notify(SyncStatus::Error, "Delete failed: " + f);
                            }
                        }
                    }

                    if (changed)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    }
                }

                notify(SyncStatus::Done, attempts <= max_attempts ? "Synced" : "Max attempts exceeded");
            }

        private:
            void notify(SyncStatus::State state, const std::string &msg,
                        uint64_t bytes = 0, uint64_t total = 0, const std::string &filename = "")
            {
                SyncStatus s;
                s.state = state;
                s.host = m_host;
                s.port = m_port;
                s.filename = filename.empty() ? msg : filename;
                s.message = msg;
                s.bytes = bytes;
                s.total = total;
                ofNotifyEvent(m_parent->syncEvent, s, m_parent);
            }

            std::string m_host;
            uint16_t m_port;
            fs::path m_local_root;
            SyncClient *m_parent;
        };

        std::vector<Target> m_targets;
        fs::path m_local_root;
    };

} // namespace tcp_file

// ---------------------------------------------------------------
// Example usage with openFrameworks (uncomment to test)
// Assume in an ofApp or similar:
/*
void ofApp::setup() {
    using namespace tcp_file;
    srv = std::make_unique<Server>(8080, ".");
    srv->startThread();
}

void ofApp::update() {
    // Client usage in update or elsewhere
    using namespace tcp_file;
    static Client cli("127.0.0.1", 8080);
    static bool connected = false;
    if (!connected) {
        connected = cli.connect();
    }
    if (connected) {
        std::cout << "Remote listing:\n" << cli.list() << "\n";
        // etc.
    }
}

void ofApp::exit() {
    srv->stopThread();
    srv->waitThread();
}
*/
#endif // TCP_FILE_TRANSFER_HPP