#include "Core.h"
#include "PacketDef.h"
#include "TinyMD5.h"

void Core::setup(bool headless) {
    bHeadless = headless;
    metro.setup();
    
    string pPath = loadSettings();
    if (pPath == "" || !ofDirectory(pPath).exists()) {
        pPath = ofFilePath::getCurrentExeDir();
    }
    
    reloadProject(pPath);
}

void Core::update() {
    watcher.update();
    warper.update();
    net.updatePeers();

    if (net.isAuthority()) {
        if (ofGetFrameNum() % 60 == 0) {
            net.sendMetronome(metro.bpm, metro.referenceTime, metro.beatsPerBar);
        }
    }

    float pct = 0.0f;
    if (incoming.total > 0)
        pct = (float)incoming.current / (float)incoming.total;
    net.setLocalSyncStatus(incoming.active, incoming.name, pct);

    handlePackets();
}

void Core::handlePackets() {
    int size = 0;
    while ((size = net.receive(packetBuffer, 65535)) > 0) {
        PacketHeader *h = (PacketHeader *)packetBuffer;
        if (h->id != PACKET_ID) {
            ofLogWarning("Core") << "Invalid packet ID: " << (int)h->id;
            continue;
        }
        if (strncmp(h->senderId, identity.myId.c_str(), 8) == 0) {
            continue; // Ignore our own packets
        }

        ofLogNotice("Core") << "Received packet type " << (int)h->type << " from " << h->senderId << ", size " << size;

        if (h->type == PKT_HEARTBEAT) {
            HeartbeatPacket *p = (HeartbeatPacket *)packetBuffer;
            net.updatePeer(p->peerId, (AppRole)p->role, p->isSyncing, p->syncProgress, p->syncingFile);
        } else if (h->type == PKT_WARP_DATA && !net.isAuthority()) {
            WarpPacket *p = (WarpPacket *)packetBuffer;
            warper.updatePeerPoint(p->ownerId, p->surfaceIndex, p->mode, p->pointIndex, p->x, p->y);
        } else if (h->type == PKT_METRONOME && !net.isAuthority()) {
            MetronomePacket *p = (MetronomePacket *)packetBuffer;
            metro.bpm = p->bpm;
            metro.referenceTime = p->referenceTime;
            metro.beatsPerBar = p->beatsPerBar;
        } else if (h->type == PKT_STRUCT && !net.isAuthority()) {
            string jStr(packetBuffer + sizeof(PacketHeader), size - sizeof(PacketHeader));
            string warpPath = ofFilePath::join(ofFilePath::join(projectPath, "configs"), "warps.json");
            ofLogNotice("Core") << "Saving PKT_STRUCT to " << warpPath << ". Content: " << jStr;
            ofBufferToFile(warpPath, ofBuffer(jStr.c_str(), jStr.length()));
            warper.loadJson(jStr);
        } else if (h->type == PKT_FILE_OFFER && !net.isAuthority()) {
            FileOfferPacket *p = (FileOfferPacket *)packetBuffer;
            string name = string(packetBuffer + sizeof(FileOfferPacket), p->nameLen);
            string remoteHash = string(p->hash, 32);
            string fullPath = ofFilePath::join(mediaDir, name);
            string localHash = TinyMD5::getFileMD5(fullPath);
            
            ofLogNotice("Core") << "File offer: " << name << " remoteHash: " << remoteHash << " localHash: " << localHash;

            if (localHash != remoteHash) {
                incoming.active = true;
                incoming.name = name;
                incoming.total = p->totalSize;
                incoming.current = 0;
                incoming.buf.allocate(p->totalSize);
            }
        } else if (h->type == PKT_FILE_CHUNK && incoming.active) {
            FileChunkPacket *p = (FileChunkPacket *)packetBuffer;
            if (p->offset + p->size <= incoming.total) {
                memcpy(incoming.buf.getData() + p->offset, packetBuffer + sizeof(FileChunkPacket), p->size);
                incoming.current += p->size;
                ofLogNotice("Core") << "Chunk received: " << incoming.current << "/" << incoming.total;
            }
        } else if (h->type == PKT_FILE_END && incoming.active) {
            incoming.active = false;
            string finalPath = ofFilePath::join(mediaDir, incoming.name);
            string tmpPath = finalPath + ".tmp";
            ofBufferToFile(tmpPath, incoming.buf);
            ofFile(tmpPath).renameTo(finalPath, true, true);
            warper.refreshContent();
            ofLogNotice("Core") << "File sync complete: " << finalPath;
        } else if (h->type == PKT_WARP_MOVE_ALL && !net.isAuthority()) {
            WarpMoveAllPacket *p = (WarpMoveAllPacket *)packetBuffer;
            auto subset = warper.getSurfacesForPeer(p->ownerId);
            if (p->surfaceIndex < subset.size()) subset[p->surfaceIndex]->moveAll(p->dx, p->dy, p->mode);
        } else if (h->type == PKT_WARP_SCALE_ALL && !net.isAuthority()) {
            WarpScaleAllPacket *p = (WarpScaleAllPacket *)packetBuffer;
            auto subset = warper.getSurfacesForPeer(p->ownerId);
            if (p->surfaceIndex < subset.size()) subset[p->surfaceIndex]->scaleAll(p->scaleFactor, glm::vec2(p->centroidX, p->centroidY), p->mode);
        }
    }
}

void Core::reloadProject(string path) {
    projectPath = path;
    ofDirectory dir(path);
    if (!dir.exists()) dir.create(true);

    string configsDir = ofFilePath::join(path, "configs");
    if (!ofDirectory(configsDir).exists()) ofDirectory(configsDir).create();

    mediaDir = ofFilePath::join(path, "media");
    if (!ofDirectory(mediaDir).exists()) ofDirectory(mediaDir).create();

    identity.setup(ofFilePath::join(configsDir, "config.json"), bHeadless);
    if (!net.isThreadRunning()) net.setup(identity.myId, mediaDir);
    else net.setMediaPath(mediaDir);

    warper.metro = &metro;
    warper.setup(ofFilePath::join(configsDir, "warps.json"), mediaDir, identity.myId);
    stateMgr.setup(ofFilePath::join(configsDir, "states.json"));
    watcher.setup(mediaDir);
    ofAddListener(watcher.filesChanged, this, &Core::onFilesChanged);
}

void Core::onFilesChanged(std::vector<std::string> &files) {
    warper.refreshContent();
    if (!net.isAuthority()) return;
    for (const auto &f : files) net.offerFile(f);
}

void Core::syncFullState() {
    ofJson root;
    map<string, ofJson> groups;
    for (auto &surf : warper.allSurfaces) groups[surf->ownerId].push_back(surf->toJson());
    for (auto &kv : groups) root["peers"][kv.first] = kv.second;
    net.sendStructure(root.dump());
}

void Core::saveSettings(string path) {
    ofJson settings;
    settings["projectPath"] = path;
    ofSaveJson(ofFilePath::join(ofFilePath::getCurrentWorkingDirectory(), "settings.json"), settings);
}

string Core::loadSettings() {
    ofFile f(ofFilePath::join(ofFilePath::getCurrentWorkingDirectory(), "settings.json"));
    if (f.exists()) {
        ofJson settings;
        f >> settings;
        return settings.value("projectPath", "");
    }
    return "";
}

void Core::exit() {
    // Cleanup if needed
}
