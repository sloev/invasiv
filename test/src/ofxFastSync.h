// ofxFastSync.h
// Single-header file sync system for openFrameworks
// Usage: 
//   Server: ofxFastSync::Server server(8888, "/path/to/sync");
//   Client: ofxFastSync::Client client("/local/path", {{"192.168.1.10", 8888}, {"192.168.1.11", 8888}});

#pragma once
#include "ofMain.h"
#include <unordered_map>
#include <set>
#include <chrono>

namespace ofxFastSync {

// Fast xxHash64 implementation (public domain)
class XXHash64 {
    static constexpr uint64_t PRIME64_1 = 11400714785074694791ULL;
    static constexpr uint64_t PRIME64_2 = 14029467366897019727ULL;
    static constexpr uint64_t PRIME64_3 = 1609587929392839161ULL;
    static constexpr uint64_t PRIME64_4 = 9650029242287828579ULL;
    static constexpr uint64_t PRIME64_5 = 2870177450012600261ULL;
    
    static uint64_t rotl64(uint64_t x, int r) { return (x << r) | (x >> (64 - r)); }
    
public:
    static uint64_t hash(const void* input, size_t len, uint64_t seed = 0) {
        const uint8_t* p = (const uint8_t*)input;
        uint64_t h64;
        
        if (len >= 32) {
            const uint8_t* const end = p + len;
            const uint8_t* const limit = end - 32;
            uint64_t v1 = seed + PRIME64_1 + PRIME64_2;
            uint64_t v2 = seed + PRIME64_2;
            uint64_t v3 = seed + 0;
            uint64_t v4 = seed - PRIME64_1;
            
            do {
                v1 += *(uint64_t*)p * PRIME64_2; v1 = rotl64(v1, 31); v1 *= PRIME64_1; p += 8;
                v2 += *(uint64_t*)p * PRIME64_2; v2 = rotl64(v2, 31); v2 *= PRIME64_1; p += 8;
                v3 += *(uint64_t*)p * PRIME64_2; v3 = rotl64(v3, 31); v3 *= PRIME64_1; p += 8;
                v4 += *(uint64_t*)p * PRIME64_2; v4 = rotl64(v4, 31); v4 *= PRIME64_1; p += 8;
            } while (p <= limit);
            
            h64 = rotl64(v1, 1) + rotl64(v2, 7) + rotl64(v3, 12) + rotl64(v4, 18);
            v1 *= PRIME64_2; v1 = rotl64(v1, 31); v1 *= PRIME64_1; h64 ^= v1;
            h64 = h64 * PRIME64_1 + PRIME64_4;
            v2 *= PRIME64_2; v2 = rotl64(v2, 31); v2 *= PRIME64_1; h64 ^= v2;
            h64 = h64 * PRIME64_1 + PRIME64_4;
            v3 *= PRIME64_2; v3 = rotl64(v3, 31); v3 *= PRIME64_1; h64 ^= v3;
            h64 = h64 * PRIME64_1 + PRIME64_4;
            v4 *= PRIME64_2; v4 = rotl64(v4, 31); v4 *= PRIME64_1; h64 ^= v4;
            h64 = h64 * PRIME64_1 + PRIME64_4;
        } else {
            h64 = seed + PRIME64_5;
        }
        
        h64 += len;
        len &= 31;
        while (len >= 8) {
            uint64_t k1 = *(uint64_t*)p;
            k1 *= PRIME64_2; k1 = rotl64(k1, 31); k1 *= PRIME64_1;
            h64 ^= k1; h64 = rotl64(h64, 27) * PRIME64_1 + PRIME64_4;
            p += 8; len -= 8;
        }
        if (len >= 4) {
            h64 ^= (*(uint32_t*)p) * PRIME64_1;
            h64 = rotl64(h64, 23) * PRIME64_2 + PRIME64_3;
            p += 4; len -= 4;
        }
        while (len > 0) {
            h64 ^= (*p) * PRIME64_5;
            h64 = rotl64(h64, 11) * PRIME64_1;
            p++; len--;
        }
        
        h64 ^= h64 >> 33; h64 *= PRIME64_2;
        h64 ^= h64 >> 29; h64 *= PRIME64_3;
        h64 ^= h64 >> 32;
        return h64;
    }
};

struct FileInfo {
    string path;
    uint64_t hash;
    uint64_t timestamp;
    bool deleted;
    
    FileInfo() : hash(0), timestamp(0), deleted(false) {}
    
    string serialize() const {
        return path + "|" + ofToString(hash) + "|" + ofToString(timestamp) + "|" + (deleted ? "1" : "0");
    }
    
    static FileInfo deserialize(const string& s) {
        FileInfo fi;
        vector<string> parts = ofSplitString(s, "|");
        if (parts.size() >= 4) {
            fi.path = parts[0];
            fi.hash = ofToInt64(parts[1]);
            fi.timestamp = ofToInt64(parts[2]);
            fi.deleted = parts[3] == "1";
        }
        return fi;
    }
};

struct DirectoryState {
    map<string, FileInfo> files;
    
    string serialize() const {
        string result = ofToString(files.size()) + "\n";
        for (auto& kv : files) {
            result += kv.second.serialize() + "\n";
        }
        return result;
    }
    
    static DirectoryState deserialize(const string& s) {
        DirectoryState ds;
        vector<string> lines = ofSplitString(s, "\n");
        if (lines.size() > 0) {
            size_t count = ofToInt(lines[0]);
            for (size_t i = 1; i <= count && i < lines.size(); i++) {
                FileInfo fi = FileInfo::deserialize(lines[i]);
                if (!fi.path.empty()) {
                    ds.files[fi.path] = fi;
                }
            }
        }
        return ds;
    }
};

// Server class
class Server : public ofThread {
    int port;
    string rootPath;
    ofxTCPServer tcp;
    bool running;
    
    uint64_t hashFile(const string& path) {
        ofBuffer buf = ofBufferFromFile(path, true);
        return XXHash64::hash(buf.getData(), buf.size());
    }
    
    DirectoryState scanDirectory() {
        DirectoryState state;
        ofDirectory dir(rootPath);
        dir.allowExt("*");
        dir.listDir();
        
        for (size_t i = 0; i < dir.size(); i++) {
            string relPath = dir.getPath(i).substr(rootPath.length());
            if (relPath[0] == '/') relPath = relPath.substr(1);
            
            FileInfo fi;
            fi.path = relPath;
            fi.timestamp = std::filesystem::last_write_time(dir.getPath(i)).time_since_epoch().count();
            
            if (dir.getFile(i).isFile()) {
                fi.hash = hashFile(dir.getPath(i));
            }
            state.files[relPath] = fi;
        }
        return state;
    }
    
    void threadedFunction() override {
        tcp.setup(port);
        tcp.setMessageDelimiter("\n###END###\n");
        running = true;
        
        while (isThreadRunning()) {
            for (int i = 0; i < tcp.getLastID(); i++) {
                if (!tcp.isClientConnected(i)) continue;
                
                string msg = tcp.receive(i);
                if (msg.empty()) continue;
                
                if (msg == "LIST") {
                    DirectoryState state = scanDirectory();
                    tcp.send(i, state.serialize() + "\n###END###\n");
                } else if (msg.substr(0, 4) == "GET:") {
                    string path = msg.substr(4);
                    string fullPath = rootPath + "/" + path;
                    ofBuffer buf = ofBufferFromFile(fullPath, true);
                    string response = "FILE:" + ofToString(buf.size()) + "\n";
                    tcp.send(i, response + buf.getText() + "\n###END###\n");
                } else if (msg.substr(0, 4) == "PUT:") {
                    size_t pathEnd = msg.find('\n');
                    string path = msg.substr(4, pathEnd - 4);
                    string content = msg.substr(pathEnd + 1);
                    
                    string fullPath = rootPath + "/" + path;
                    ofFile file(fullPath, ofFile::WriteOnly, true);
                    file.create();
                    file.write(content);
                    file.close();
                    
                    tcp.send(i, "OK\n###END###\n");
                } else if (msg.substr(0, 4) == "DEL:") {
                    string path = msg.substr(4);
                    string fullPath = rootPath + "/" + path;
                    ofFile::removeFile(fullPath);
                    tcp.send(i, "OK\n###END###\n");
                }
            }
            ofSleepMillis(10);
        }
        tcp.close();
    }
    
public:
    Server(int port, const string& rootPath) : port(port), rootPath(rootPath), running(false) {
        startThread();
    }
    
    ~Server() {
        stopThread();
        waitForThread(true);
    }
};

// Client class
class Client : public ofThread {
    struct Remote {
        string ip;
        int port;
        DirectoryState cachedState;
    };
    
    string localPath;
    vector<Remote> remotes;
    DirectoryState localState;
    uint64_t lastCheckTime;
    bool running;
    ofMutex mtx;
    
    uint64_t hashFile(const string& path) {
        ofBuffer buf = ofBufferFromFile(path, true);
        return XXHash64::hash(buf.getData(), buf.size());
    }
    
    DirectoryState scanDirectory() {
        DirectoryState state;
        ofDirectory dir(localPath);
        dir.allowExt("*");
        dir.listDir();
        
        for (size_t i = 0; i < dir.size(); i++) {
            string relPath = dir.getPath(i).substr(localPath.length());
            if (relPath[0] == '/') relPath = relPath.substr(1);
            
            FileInfo fi;
            fi.path = relPath;
            fi.timestamp = std::filesystem::last_write_time(dir.getPath(i)).time_since_epoch().count();
            
            if (dir.getFile(i).isFile()) {
                fi.hash = hashFile(dir.getPath(i));
            }
            state.files[relPath] = fi;
        }
        return state;
    }
    
    DirectoryState fetchRemoteList(const string& ip, int port) {
        ofxTCPClient tcp;
        tcp.setup(ip, port);
        tcp.setMessageDelimiter("\n###END###\n");
        
        if (!tcp.isConnected()) return DirectoryState();
        
        tcp.send("LIST\n###END###\n");
        
        string response;
        int attempts = 0;
        while (response.empty() && attempts++ < 50) {
            response = tcp.receive();
            ofSleepMillis(20);
        }
        
        tcp.close();
        return DirectoryState::deserialize(response);
    }
    
    void syncToRemote(Remote& remote) {
        ofxTCPClient tcp;
        tcp.setup(remote.ip, remote.port);
        tcp.setMessageDelimiter("\n###END###\n");
        
        if (!tcp.isConnected()) return;
        
        // Find changes
        for (auto& kv : localState.files) {
            const FileInfo& local = kv.second;
            auto remoteIt = remote.cachedState.files.find(kv.first);
            
            bool needsUpdate = false;
            if (remoteIt == remote.cachedState.files.end()) {
                needsUpdate = true; // New file
            } else if (remoteIt->second.hash != local.hash) {
                needsUpdate = true; // Modified file
            }
            
            if (needsUpdate) {
                string fullPath = localPath + "/" + local.path;
                ofBuffer buf = ofBufferFromFile(fullPath, true);
                string msg = "PUT:" + local.path + "\n" + buf.getText() + "\n###END###\n";
                tcp.send(msg);
                
                string response;
                int attempts = 0;
                while (response.empty() && attempts++ < 50) {
                    response = tcp.receive();
                    ofSleepMillis(20);
                }
            }
        }
        
        // Find deletions
        for (auto& kv : remote.cachedState.files) {
            if (localState.files.find(kv.first) == localState.files.end()) {
                tcp.send("DEL:" + kv.first + "\n###END###\n");
                
                string response;
                int attempts = 0;
                while (response.empty() && attempts++ < 50) {
                    response = tcp.receive();
                    ofSleepMillis(20);
                }
            }
        }
        
        tcp.close();
        
        // Update cache
        lock();
        remote.cachedState = localState;
        unlock();
    }
    
    void threadedFunction() override {
        running = true;
        lastCheckTime = ofGetElapsedTimeMillis();
        
        // Initial sync
        localState = scanDirectory();
        
        // Fetch initial remote states
        for (auto& remote : remotes) {
            remote.cachedState = fetchRemoteList(remote.ip, remote.port);
        }
        
        while (isThreadRunning()) {
            uint64_t now = ofGetElapsedTimeMillis();
            
            if (now - lastCheckTime >= 1000) {
                lastCheckTime = now;
                
                DirectoryState newState = scanDirectory();
                
                // Check if anything changed
                bool changed = newState.files.size() != localState.files.size();
                if (!changed) {
                    for (auto& kv : newState.files) {
                        auto it = localState.files.find(kv.first);
                        if (it == localState.files.end() || it->second.hash != kv.second.hash) {
                            changed = true;
                            break;
                        }
                    }
                }
                
                if (changed) {
                    lock();
                    localState = newState;
                    unlock();
                    
                    // Sync to all remotes in parallel using thread pool
                    vector<std::thread> threads;
                    for (auto& remote : remotes) {
                        threads.emplace_back([this, &remote]() {
                            syncToRemote(remote);
                        });
                    }
                    
                    // Wait for all syncs to complete
                    for (auto& t : threads) {
                        if (t.joinable()) t.join();
                    }
                }
            }
            
            ofSleepMillis(100);
        }
    }
    
public:
    Client(const string& localPath, const vector<pair<string, int>>& remoteList) 
        : localPath(localPath), lastCheckTime(0), running(false) {
        
        for (auto& r : remoteList) {
            Remote remote;
            remote.ip = r.first;
            remote.port = r.second;
            remotes.push_back(remote);
        }
        
        startThread();
    }
    
    ~Client() {
        stopThread();
        waitForThread(true);
    }
    
    void addRemote(const string& ip, int port) {
        lock();
        Remote remote;
        remote.ip = ip;
        remote.port = port;
        remotes.push_back(remote);
        unlock();
    }
};

} // namespace ofxFastSync