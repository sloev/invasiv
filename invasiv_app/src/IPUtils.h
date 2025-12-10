#pragma once
#include <string>
#include <vector>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>     
#include <sys/types.h>
#include <cstring>
#include "ofLog.h"

class IPUtils {
public:
    static std::string getBroadcastAddress() {
        struct ifaddrs *interfaces = nullptr;
        struct ifaddrs *temp_addr = nullptr;
        std::string broadcastIP = "255.255.255.255"; 

        if (getifaddrs(&interfaces) == 0) {
            temp_addr = interfaces;
            while (temp_addr != nullptr) {
                if (temp_addr->ifa_addr != nullptr && temp_addr->ifa_addr->sa_family == AF_INET) {
                    bool isUp = (temp_addr->ifa_flags & IFF_UP);
                    bool isLoopback = (temp_addr->ifa_flags & IFF_LOOPBACK);
                    bool isRunning = (temp_addr->ifa_flags & IFF_RUNNING);

                    if (isUp && !isLoopback && isRunning) {
                        // CRITICAL FIX: Check if broadaddr is actually valid before accessing
                        if (temp_addr->ifa_broadaddr != nullptr) {
                            void* ptr = &((struct sockaddr_in *)temp_addr->ifa_broadaddr)->sin_addr;
                            char buffer[INET_ADDRSTRLEN];
                            // Initialize buffer to prevent garbage data
                            memset(buffer, 0, INET_ADDRSTRLEN);
                            
                            if(inet_ntop(AF_INET, ptr, buffer, INET_ADDRSTRLEN)) {
                                std::string foundIP(buffer);
                                if (!foundIP.empty()) {
                                    broadcastIP = foundIP;
                                    ofLogNotice("IPUtils") << "Interface: " << temp_addr->ifa_name << " Broadcast: " << broadcastIP;
                                    break; 
                                }
                            }
                        }
                    }
                }
                temp_addr = temp_addr->ifa_next;
            }
        }
        if (interfaces) freeifaddrs(interfaces);
        return broadcastIP;
    }
};