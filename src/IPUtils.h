#pragma once
#include <string>
#include <vector>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>     
#include <sys/types.h>
#include <cstring>
#include <cstdlib>
#include "ofLog.h"

class IPUtils {
public:
    static std::string getBroadcastAddress() {
        // Check for test override
        const char* testAddr = std::getenv("INVASIV_TEST_ADDR");
        if (testAddr != nullptr) {
            ofLogNotice("IPUtils") << "TEST_MODE: Using override address: " << testAddr;
            return std::string(testAddr);
        }

        struct ifaddrs *interfaces = nullptr;
        struct ifaddrs *temp_addr = nullptr;
        std::string broadcastIP = ""; 

        if (getifaddrs(&interfaces) == 0) {
            temp_addr = interfaces;
            while (temp_addr != nullptr) {
                if (temp_addr->ifa_addr != nullptr && temp_addr->ifa_addr->sa_family == AF_INET) {
                    bool isUp = (temp_addr->ifa_flags & IFF_UP);
                    bool isLoopback = (temp_addr->ifa_flags & IFF_LOOPBACK);
                    bool isRunning = (temp_addr->ifa_flags & IFF_RUNNING);

                    if (isUp && isRunning) {
                        if (isLoopback) {
                            if (broadcastIP == "") broadcastIP = "127.255.255.255";
                        } else if (temp_addr->ifa_broadaddr != nullptr) {
                            void* ptr = &((struct sockaddr_in *)temp_addr->ifa_broadaddr)->sin_addr;
                            char buffer[INET_ADDRSTRLEN];
                            memset(buffer, 0, INET_ADDRSTRLEN);
                            if(inet_ntop(AF_INET, ptr, buffer, INET_ADDRSTRLEN)) {
                                broadcastIP = std::string(buffer);
                                break; 
                            }
                        }
                    }
                }
                temp_addr = temp_addr->ifa_next;
            }
        }
        if (interfaces) freeifaddrs(interfaces);
        if (broadcastIP == "" || broadcastIP == "0.0.0.0") broadcastIP = "255.255.255.255";
        return broadcastIP;
    }
};
