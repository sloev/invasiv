// preferred_ip_broadcast.hpp
// ------------------------------------------------------------
//  Portable C++17 header-only library
//  Returns:
//    * preferred outbound IPv4 address
//    * broadcast address of the same subnet
// ------------------------------------------------------------

#pragma once
#include <string>
#include <optional>
#include <cstdint>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <unistd.h>
#endif

namespace netinfo
{

    /* -------------------------------------------------------------
       Helper: IPv4 <-> uint32_t
       ------------------------------------------------------------- */
    inline uint32_t ip_to_u32(const std::string &ip)
    {
        in_addr a{};
        inet_pton(AF_INET, ip.c_str(), &a);
        return ntohl(a.s_addr);
    }

    inline std::string u32_to_ip(uint32_t v)
    {
        in_addr a{};
        a.s_addr = htonl(v);
        char buf[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &a, buf, sizeof(buf));
        return buf;
    }

    /* -------------------------------------------------------------
       Get the IP the OS would use to reach the Internet
       ------------------------------------------------------------- */
    inline std::string preferred_ip()
    {
#if defined(_WIN32)
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
            return "";

        SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
        if (s == INVALID_SOCKET)
        {
            WSACleanup();
            return "";
        }

        sockaddr_in remote{};
        remote.sin_family = AF_INET;
        remote.sin_port = htons(1);
        inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr);

        if (connect(s, (sockaddr *)&remote, sizeof(remote)) == SOCKET_ERROR)
        {
            closesocket(s);
            WSACleanup();
            return "";
        }

        sockaddr_in local{};
        socklen_t len = sizeof(local);
        if (getsockname(s, (sockaddr *)&local, &len) == SOCKET_ERROR)
        {
            closesocket(s);
            WSACleanup();
            return "";
        }

        char buf[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf));

        closesocket(s);
        WSACleanup();
        return std::string(buf);

#else // POSIX
        int s = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (s < 0)
            return "";

        sockaddr_in remote{};
        remote.sin_family = AF_INET;
        remote.sin_port = htons(1);
        inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr);

        if (connect(s, (sockaddr *)&remote, sizeof(remote)) < 0)
        {
            close(s);
            return "";
        }

        sockaddr_in local{};
        socklen_t len = sizeof(local);
        if (getsockname(s, (sockaddr *)&local, &len) < 0)
        {
            close(s);
            return "";
        }

        char buf[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf));
        close(s);
        return std::string(buf);
#endif
    }

    /* -------------------------------------------------------------
       Get broadcast address for a given IPv4 address
       ------------------------------------------------------------- */
    inline std::string broadcast_for_ip(const std::string &ip)
    {
#if defined(_WIN32)

        ULONG bufLen = 16384;
        std::vector<uint8_t> buffer(bufLen);
        PIP_ADAPTER_ADDRESSES pAddresses = nullptr;

        DWORD dwRet = GetAdaptersAddresses(AF_INET,
                                           GAA_FLAG_INCLUDE_PREFIX,
                                           nullptr,
                                           (PIP_ADAPTER_ADDRESSES)buffer.data(),
                                           &bufLen);
        if (dwRet == ERROR_BUFFER_OVERFLOW)
        {
            buffer.resize(bufLen);
            dwRet = GetAdaptersAddresses(AF_INET,
                                         GAA_FLAG_INCLUDE_PREFIX,
                                         nullptr,
                                         (PIP_ADAPTER_ADDRESSES)buffer.data(),
                                         &bufLen);
        }
        if (dwRet != NO_ERROR)
            return {};

        pAddresses = (PIP_ADAPTER_ADDRESSES)buffer.data();
        uint32_t target = ip_to_u32(ip);

        for (PIP_ADAPTER_ADDRESSES pCurr = pAddresses; pCurr; pCurr = pCurr->Next)
        {
            for (PIP_ADAPTER_UNICAST_ADDRESS pUnicast = pCurr->FirstUnicastAddress;
                 pUnicast; pUnicast = pUnicast->Next)
            {

                if (pUnicast->Address.lpSockaddr->sa_family != AF_INET)
                    continue;
                sockaddr_in *sin = (sockaddr_in *)pUnicast->Address.lpSockaddr;
                if (ntohl(sin->sin_addr.s_addr) == target)
                {

                    // Look for prefix (netmask)
                    for (PIP_ADAPTER_PREFIX pPrefix = pCurr->FirstPrefix; pPrefix; pPrefix = pPrefix->Next)
                    {
                        if (pPrefix->Address.lpSockaddr->sa_family == AF_INET)
                        {
                            uint32_t mask = 0xFFFFFFFFu << (32 - pPrefix->PrefixLength);
                            uint32_t net = target & mask;
                            uint32_t bcast = net | ~mask;
                            return u32_to_ip(bcast);
                        }
                    }
                }
            }
        }
        return "";

#else // POSIX (Linux / macOS)

        struct ifaddrs *ifap = nullptr, *ifa = nullptr;
        if (getifaddrs(&ifap) != 0)
            return {};

        uint32_t target = ip_to_u32(ip);
        std::string result = "";

        for (ifa = ifap; ifa; ifa = ifa->ifa_next)
        {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
                continue;

            sockaddr_in *addr = (sockaddr_in *)ifa->ifa_addr;
            if (ntohl(addr->sin_addr.s_addr) != target)
                continue;

            if (ifa->ifa_netmask)
            {
                sockaddr_in *mask = (sockaddr_in *)ifa->ifa_netmask;
                uint32_t m = ntohl(mask->sin_addr.s_addr);
                uint32_t net = target & m;
                uint32_t bcast = net | ~m;
                result = u32_to_ip(bcast);
                break;
            }
        }

        freeifaddrs(ifap);
        return result;
#endif
    }

    /* -------------------------------------------------------------
       Convenience: both values in one call
       ------------------------------------------------------------- */
    struct ip_pair
    {
        std::string preferred;
        std::string broadcast;
    };

    inline ip_pair preferred_and_broadcast()
    {
        ip_pair pair;
        pair.preferred = preferred_ip();

        pair.broadcast = broadcast_for_ip(pair.preferred);
        return pair;
    }

}