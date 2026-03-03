#include "Network.h"

Network::~Network()
{
    ofLogNotice("Network") << "Shutting down network thread...";
    stopThread();
    waitForThread(true);
}

void Network::setup(string _id, string _mediaPath)
{
    myId = _id;
    mediaPath = _mediaPath;
    setupSocket();
    startThread();
}

void Network::setupSocket()
{
    string broadcastIP = IPUtils::getBroadcastAddress();
    
    if(broadcastIP == "0.0.0.0" || broadcastIP == "") broadcastIP = "192.168.1.255";

    ofLogNotice("Network") << "Binding to Broadcast: " << broadcastIP;

    listener.Close();
    listener.Create();
    listener.SetReuseAddress(true);
    listener.Bind(9000);
    listener.SetNonBlocking(true);

    sender.Close();
    sender.Create();
    sender.SetReuseAddress(true);
    sender.SetEnableBroadcast(true);
    sender.Connect(broadcastIP.c_str(), 9000);
    sender.SetNonBlocking(true);
}

void Network::setMediaPath(string _path)
{
    lock();
    mediaPath = _path;
    unlock();
}

void Network::setRole(AppRole newRole)
{
    lock();
    role = newRole;
    if (role == ROLE_MASTER_EDIT) ofLogNotice() << "Switched to MASTER EDIT";
    else if (role == ROLE_MASTER_PERFORM) ofLogNotice() << "Switched to MASTER PERFORM";
    else ofLogNotice() << "Switched to PEER";
    unlock();
}

AppRole Network::getMasterRole()
{
    for (auto &p : peers) {
        if (p.second.role != ROLE_PEER) return p.second.role;
    }
    return ROLE_PEER;
}

void Network::setLocalSyncStatus(bool syncing, string filename, float progress)
{
    lock();
    myIsSyncing = syncing;
    mySyncFile = filename;
    mySyncProgress = progress;
    unlock();
}

bool Network::hasActiveMaster()
{
    for (auto &p : peers) {
        if (p.second.role != ROLE_PEER) return true;
    }
    return false;
}

void Network::sendHeartbeat()
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

void Network::sendWarpMoveAll(string ownerId, int surfIdx, int mode, float dx, float dy)
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

void Network::sendWarpScaleAll(string ownerId, int surfIdx, int mode, float factor, float cx, float cy)
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

void Network::sendMetronome(float bpm, double refTime, int beats)
{
    if (!isAuthority() || inErrorState) return;
    MetronomePacket p;
    fillHeader(p.header, PKT_METRONOME);
    p.bpm = bpm;
    p.referenceTime = refTime;
    p.beatsPerBar = (uint8_t)beats;
    sendSafe((const char *)&p, sizeof(MetronomePacket));
}

void Network::sendFullscreen(string targetId, bool enabled)
{
    if (!isAuthority() || inErrorState) return;
    FullscreenPacket p;
    fillHeader(p.header, PKT_FULLSCREEN);
    strncpy(p.targetId, targetId.c_str(), 8);
    p.targetId[8] = 0;
    p.enabled = enabled;
    sendSafe((const char *)&p, sizeof(FullscreenPacket));
}

void Network::sendWarp(string ownerId, int surfIdx, int mode, int ptIdx, float x, float y)
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

void Network::sendStructure(string jsonStr)
{
    if (!isAuthority() || inErrorState) return;
    PacketHeader h;
    fillHeader(h, PKT_STRUCT);
    vector<char> buf(sizeof(PacketHeader) + jsonStr.length());
    memcpy(buf.data(), &h, sizeof(PacketHeader));
    memcpy(buf.data() + sizeof(PacketHeader), jsonStr.c_str(), jsonStr.length());
    sendSafe(buf.data(), buf.size());
}

void Network::offerFile(string filename)
{
    if (!isAuthority()) return;
    lock();
    pendingFiles.push(filename);
    unlock();
}

int Network::receive(char *buf, int max)
{
    return listener.Receive(buf, max);
}

void Network::updatePeers()
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

void Network::updatePeer(string id, AppRole role, bool syncing, float progress, string file)
{
    PeerData &p = peers[id];
    p.id = id;
    p.role = role;
    p.isSyncing = syncing;
    p.syncProgress = progress;
    p.syncingFile = file;
    p.lastSeen = ofGetElapsedTimef();
}

void Network::fillHeader(PacketHeader &h, uint8_t type)
{
    h.id = PACKET_ID;
    h.type = type;
    memset(h.senderId, 0, 9);
    strncpy(h.senderId, myId.c_str(), 8);
}

void Network::sendSafe(const char* data, int size) {
    if(inErrorState) return;

    int result = sender.Send(data, size);
    
    if (result == -1) {
        ofLogError("Network") << "Send failed. Pausing network for " << ERROR_BACKOFF_SEC << "s";
        inErrorState = true;
        errorRecoverTime = ofGetElapsedTimef() + ERROR_BACKOFF_SEC;
    }
}

void Network::threadedFunction()
{
    float lastHeartbeat = 0;
    while (isThreadRunning())
    {
        if (inErrorState) {
            if (ofGetElapsedTimef() > errorRecoverTime) {
                ofLogNotice("Network") << "Attempting to recover network...";
                setupSocket(); 
                inErrorState = false;
            } else {
                sleep(100);
                continue; 
            }
        }

        float now = ofGetElapsedTimef();
        if (now - lastHeartbeat > 1.0f) {
            sendHeartbeat();
            lastHeartbeat = now;
        }

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

void Network::transferFile(string filename)
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
    if(inErrorState) return;

    sleep(100);

    ofBuffer fBuf = file.readToBuffer();
    uint32_t offset = 0;
    uint16_t chunk = 1024;
    while (offset < size)
    {
        if(inErrorState) return;

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
