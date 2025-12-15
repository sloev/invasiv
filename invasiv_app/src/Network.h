#pragma once
#include "ofMain.h"
#include "ofxNetwork.h"
#include "PacketDef.h"
#include "TinyMD5.h"
#include "IPUtils.h"
#include <queue>
#include <map>

class Network : public ofThread
{
public:
    struct PeerData
    {
        string id;
        AppRole role; 
        float lastSeen;
        bool isSyncing;
        float syncProgress;
        string syncingFile;
    };

    map<string, PeerData> peers;
    
    // State Management
    AppRole role = ROLE_PEER;
    
    string myId;
    string mediaPath;

    ~Network()
    {
        ofLogNotice("Network") << "Shutting down network thread...";
        stopThread();
        waitForThread(true);
    }

    void setup(string _id, string _mediaPath)
    {
        myId = _id;
        mediaPath = _mediaPath;
        setupSocket();
        startThread();
    }

    // Attempt to re-bind sockets if network comes back or changes
    void setupSocket()
    {
        string broadcastIP = IPUtils::getBroadcastAddress();
        
        // Basic check to see if we got a valid IP, otherwise default to standard local broadcast
        if(broadcastIP == "0.0.0.0" || broadcastIP == "") broadcastIP = "192.168.1.255";

        ofLogNotice("Network") << "Binding to Broadcast: " << broadcastIP;

        listener.Close();
        listener.Create();
        listener.SetReuseAddress(true);
        listener.Bind(9000);
        listener.SetNonBlocking(true);

        sender.Close();
        sender.Create();
        sender.SetEnableBroadcast(true);
        sender.Connect(broadcastIP.c_str(), 9000);
        sender.SetNonBlocking(true);
    }

    void setMediaPath(string _path)
    {
        lock();
        mediaPath = _path;
        unlock();
    }

    void setRole(AppRole newRole)
    {
        lock();
        role = newRole;
        if (role == ROLE_MASTER_EDIT) ofLogNotice() << "Switched to MASTER EDIT";
        else if (role == ROLE_MASTER_PERFORM) ofLogNotice() << "Switched to MASTER PERFORM";
        else ofLogNotice() << "Switched to PEER";
        unlock();
    }

    bool isAuthority() { return role != ROLE_PEER; }
    bool isEditing() { return role == ROLE_MASTER_EDIT; }
    
    AppRole getMasterRole()
    {
        for (auto &p : peers) {
            if (p.second.role != ROLE_PEER) return p.second.role;
        }
        return ROLE_PEER;
    }

    void setLocalSyncStatus(bool syncing, string filename, float progress)
    {
        lock();
        myIsSyncing = syncing;
        mySyncFile = filename;
        mySyncProgress = progress;
        unlock();
    }

    bool hasActiveMaster()
    {
        for (auto &p : peers) {
            if (p.second.role != ROLE_PEER) return true;
        }
        return false;
    }

    // --- Sending Functions (Wrapped) ---

    void sendHeartbeat()
    {
        if(inErrorState) return;

        HeartbeatPacket p;
        fillHeader(p.header, PKT_HEARTBEAT);
        p.role = (uint8_t)role;
        strncpy(p.peerId, myId.c_str(), 8);
        p.peerId[8] = 0;

        lock();
        p.isSyncing = myIsSyncing;
        p.syncProgress = mySyncProgress;
        memset(p.syncingFile, 0, 64);
        strncpy(p.syncingFile, mySyncFile.c_str(), 63);
        unlock();

        sendSafe((const char *)&p, sizeof(HeartbeatPacket));
    }

    void sendWarpMoveAll(string ownerId, int surfIdx, int mode, float dx, float dy)
    {
        if (!isAuthority() || inErrorState) return;
        WarpMoveAllPacket p;
        fillHeader(p.header, PKT_WARP_MOVE_ALL);
        strncpy(p.ownerId, ownerId.c_str(), 8);
        p.ownerId[8] = 0;
        p.surfaceIndex = surfIdx;
        p.mode = mode;
        p.dx = dx;
        p.dy = dy;
        sendSafe((const char *)&p, sizeof(WarpMoveAllPacket));
    }

    void sendWarpScaleAll(string ownerId, int surfIdx, int mode, float factor, float cx, float cy)
    {
        if (!isAuthority() || inErrorState) return;
        WarpScaleAllPacket p;
        fillHeader(p.header, PKT_WARP_SCALE_ALL);
        strncpy(p.ownerId, ownerId.c_str(), 8);
        p.ownerId[8] = 0;
        p.surfaceIndex = surfIdx;
        p.mode = mode;
        p.scaleFactor = factor;
        p.centroidX = cx;
        p.centroidY = cy;
        sendSafe((const char *)&p, sizeof(WarpScaleAllPacket));
    }

    void sendWarp(string ownerId, int surfIdx, int mode, int ptIdx, float x, float y)
    {
        if (!isAuthority() || inErrorState) return;
        WarpPacket p;
        fillHeader(p.header, PKT_WARP_DATA);
        strncpy(p.ownerId, ownerId.c_str(), 8);
        p.ownerId[8] = 0;
        p.surfaceIndex = surfIdx;
        p.mode = mode;
        p.pointIndex = ptIdx;
        p.x = x;
        p.y = y;
        sendSafe((const char *)&p, sizeof(WarpPacket));
    }

    void sendStructure(string jsonStr)
    {
        if (!isAuthority() || inErrorState) return;
        PacketHeader h;
        fillHeader(h, PKT_STRUCT);
        vector<char> buf(sizeof(PacketHeader) + jsonStr.length());
        memcpy(buf.data(), &h, sizeof(PacketHeader));
        memcpy(buf.data() + sizeof(PacketHeader), jsonStr.c_str(), jsonStr.length());
        sendSafe(buf.data(), buf.size());
    }

    void offerFile(string filename)
    {
        if (!isAuthority()) return;
        lock();
        pendingFiles.push(filename);
        unlock();
    }

    int receive(char *buf, int max)
    {
        return listener.Receive(buf, max);
    }

    void updatePeers()
    {
        float now = ofGetElapsedTimef();
        for (auto it = peers.begin(); it != peers.end();)
        {
            if (now - it->second.lastSeen > 5.0)
                it = peers.erase(it);
            else
                ++it;
        }
    }

private:
    ofxUDPManager sender;
    ofxUDPManager listener;
    std::queue<string> pendingFiles;

    bool myIsSyncing = false;
    string mySyncFile = "";
    float mySyncProgress = 0.0f;

    // -- Error Handling Vars --
    bool inErrorState = false;
    float errorRecoverTime = 0.0f;
    const float ERROR_BACKOFF_SEC = 2.0f;

    void fillHeader(PacketHeader &h, uint8_t type)
    {
        h.id = PACKET_ID;
        h.type = type;
        memset(h.senderId, 0, 9);
        strncpy(h.senderId, myId.c_str(), 8);
    }
    
    // Wrapper to catch ENETUNREACH and chill out
    void sendSafe(const char* data, int size) {
        if(inErrorState) return;

        int result = sender.Send(data, size);
        
        if (result == -1) {
            ofLogError("Network") << "Send failed (Network Unreachable?). Pausing network for " << ERROR_BACKOFF_SEC << "s";
            inErrorState = true;
            errorRecoverTime = ofGetElapsedTimef() + ERROR_BACKOFF_SEC;
        }
    }

    void threadedFunction()
    {
        while (isThreadRunning())
        {
            // 1. Check if we are in a penalty box
            if (inErrorState) {
                if (ofGetElapsedTimef() > errorRecoverTime) {
                    ofLogNotice("Network") << "Attempting to recover network...";
                    setupSocket(); // Try to rebind
                    inErrorState = false;
                } else {
                    sleep(100); // Sleep and do nothing
                    continue; 
                }
            }

            // 2. Normal Operation
            if (ofGetFrameNum() % 60 == 0)
                sendHeartbeat();

            string filename = "";
            lock();
            if (!pendingFiles.empty())
            {
                filename = pendingFiles.front();
                pendingFiles.pop();
            }
            unlock();

            if (filename != "")
                transferFile(filename);

            sleep(16);
        }
    }

    void transferFile(string filename)
    {
        if(inErrorState) return;

        string fullPath = ofFilePath::join(mediaPath, filename);
        string hash = TinyMD5::getFileMD5(fullPath);
        ofFile file(fullPath, ofFile::ReadOnly, true);

        if (!file.exists()) return;
        uint32_t size = file.getSize();

        int headSize = sizeof(FileOfferPacket);
        vector<char> buf(headSize + filename.length());
        FileOfferPacket *offer = (FileOfferPacket *)buf.data();

        fillHeader(offer->header, PKT_FILE_OFFER);
        offer->totalSize = size;
        offer->nameLen = filename.length();
        strncpy(offer->hash, hash.c_str(), 32);
        offer->hash[32] = 0;
        memcpy(buf.data() + headSize, filename.c_str(), filename.length());
        
        sendSafe(buf.data(), buf.size());
        if(inErrorState) return; // Abort if fail

        sleep(100);

        ofBuffer fBuf = file.readToBuffer();
        uint32_t offset = 0;
        uint16_t chunk = 1024;
        while (offset < size)
        {
            if(inErrorState) return; // Abort mid-transfer if network dies

            uint16_t cur = std::min((uint32_t)chunk, size - offset);
            vector<char> cBuf(sizeof(FileChunkPacket) + cur);
            FileChunkPacket *p = (FileChunkPacket *)cBuf.data();

            fillHeader(p->header, PKT_FILE_CHUNK);
            p->offset = offset;
            p->size = cur;
            memcpy(cBuf.data() + sizeof(FileChunkPacket), fBuf.getData() + offset, cur);
            
            sendSafe(cBuf.data(), cBuf.size());
            
            offset += cur;
            sleep(2);
        }

        PacketHeader end;
        fillHeader(end, PKT_FILE_END);
        sendSafe((const char *)&end, sizeof(PacketHeader));
    }
};