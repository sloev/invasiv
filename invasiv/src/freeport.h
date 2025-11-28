// free_port.hpp
#pragma once
#include <cstdint>

#ifdef _WIN32
    #include <winsock2.h>
    #pragma comment(lib, "Ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>
    #include <arpa/inet.h>
#endif

inline uint16_t get_free_port() {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa)) return 0;
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
#else
    int s = socket(AF_INET, SOCK_STREAM, 0);
#endif

    if (s == 
#ifdef _WIN32
        INVALID_SOCKET
#else
        -1
#endif
    ) {
#ifdef _WIN32
        WSACleanup();
#endif
        return 0;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0;  // OS assigns free port

    if (bind(s, (sockaddr*)&addr, sizeof(addr)) 
#ifdef _WIN32
        == SOCKET_ERROR
#else
        < 0
#endif
    ) {
#ifdef _WIN32
        closesocket(s); WSACleanup();
#else
        close(s);
#endif
        return 0;
    }

    socklen_t len = sizeof(addr);
    if (getsockname(s, (sockaddr*)&addr, &len)
#ifdef _WIN32
        == SOCKET_ERROR
#else
        < 0
#endif
    ) {
#ifdef _WIN32
        closesocket(s); WSACleanup();
#else
        close(s);
#endif
        return 0;
    }

    uint16_t port = ntohs(addr.sin_port);

#ifdef _WIN32
    closesocket(s);
    WSACleanup();
#else
    close(s);
#endif

    return port;
}