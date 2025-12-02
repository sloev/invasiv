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
        bool isMaster;
        float lastSeen;
    };

    map<string, PeerData> peers;
    bool isMaster = false;
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

        string broadcastIP = IPUtils::getBroadcastAddress();
        ofLogNotice("Network") << "Binding to Broadcast: " << broadcastIP;

        listener.Create();
        listener.SetReuseAddress(true);
        listener.Bind(9000);
        listener.SetNonBlocking(true);

        sender.Create();
        sender.SetEnableBroadcast(true);
        sender.Connect(broadcastIP.c_str(), 9000);
        sender.SetNonBlocking(true);

        startThread();
    }

    void setMediaPath(string _path)
    {
        lock();
        mediaPath = _path;
        unlock();
    }

    void setRole(bool master)
    {
        lock();
        isMaster = master;
        if (isMaster)
            ofLogNotice() << "Switched to MASTER";
        else
            ofLogNotice() << "Switched to PEER";
        unlock();
    }

    bool hasActiveMaster()
    {
        for (auto &p : peers)
        {
            if (p.second.isMaster)
                return true;
        }
        return false;
    }

    void sendHeartbeat()
    {
        HeartbeatPacket p;
        fillHeader(p.header, PKT_HEARTBEAT);
        p.isMaster = isMaster;
        strncpy(p.peerId, myId.c_str(), 8);
        p.peerId[8] = 0;
        sender.Send((const char *)&p, sizeof(HeartbeatPacket));
    }

    void sendWarp(string ownerId, int surfIdx, int mode, int ptIdx, float x, float y)
    {
        if (!isMaster) return;
        WarpPacket p;
        fillHeader(p.header, PKT_WARP_DATA);
        strncpy(p.ownerId, ownerId.c_str(), 8);
        p.ownerId[8] = 0;
        p.surfaceIndex = surfIdx;
        p.mode = mode;
        p.pointIndex = ptIdx;
        p.x = x;
        p.y = y;
        sender.Send((const char *)&p, sizeof(WarpPacket));
    }

    void sendStructure(string jsonStr)
    {
        if (!isMaster) return;
        
        PacketHeader h;
        fillHeader(h, PKT_STRUCT);

        // Construct buffer: Header + JSON string
        vector<char> buf(sizeof(PacketHeader) + jsonStr.length());
        memcpy(buf.data(), &h, sizeof(PacketHeader));
        memcpy(buf.data() + sizeof(PacketHeader), jsonStr.c_str(), jsonStr.length());
        
        sender.Send(buf.data(), buf.size());
    }

    void offerFile(string filename)
    {
        if (!isMaster) return;
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

    // Helper to fill ID, Type, and SenderID for every packet
    void fillHeader(PacketHeader &h, uint8_t type) {
        h.id = PACKET_ID;
        h.type = type;
        memset(h.senderId, 0, 9);
        strncpy(h.senderId, myId.c_str(), 8);
    }

    void threadedFunction()
    {
        while (isThreadRunning())
        {
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
        string fullPath = ofFilePath::join(mediaPath, filename);
        string hash = TinyMD5::getFileMD5(fullPath);
        ofFile file(fullPath, ofFile::ReadOnly, true);

        if (!file.exists()) return;
        uint32_t size = file.getSize();
        
        // 1. Send Offer
        int headSize = sizeof(FileOfferPacket);
        vector<char> buf(headSize + filename.length());
        FileOfferPacket *offer = (FileOfferPacket *)buf.data();
        
        fillHeader(offer->header, PKT_FILE_OFFER);
        offer->totalSize = size;
        offer->nameLen = filename.length();
        strncpy(offer->hash, hash.c_str(), 32);
        offer->hash[32] = 0; // Null terminate hash just in case
        memcpy(buf.data() + headSize, filename.c_str(), filename.length());
        sender.Send(buf.data(), buf.size());

        sleep(100);

        // 2. Send Chunks
        ofBuffer fBuf = file.readToBuffer();
        uint32_t offset = 0;
        uint16_t chunk = 1024;
        while (offset < size)
        {
            uint16_t cur = std::min((uint32_t)chunk, size - offset);
            vector<char> cBuf(sizeof(FileChunkPacket) + cur);
            FileChunkPacket *p = (FileChunkPacket *)cBuf.data();
            
            fillHeader(p->header, PKT_FILE_CHUNK);
            p->offset = offset;
            p->size = cur;
            memcpy(cBuf.data() + sizeof(FileChunkPacket), fBuf.getData() + offset, cur);
            sender.Send(cBuf.data(), cBuf.size());
            offset += cur;
            sleep(2);
        }

        // 3. Send End
        PacketHeader end;
        fillHeader(end, PKT_FILE_END);
        sender.Send((const char *)&end, sizeof(PacketHeader));
    }
};