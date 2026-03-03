#include "ofApp.h"
#include "PacketDef.h"
#include "TinyMD5.h"
#include <filesystem>

#ifndef VERSION_NAME
#define VERSION_NAME "dev"
#endif

void ofApp::setup()
{
    metro.setup();
    ofSetFrameRate(60);
    
    if (!bHeadless) {
        ofSetVerticalSync(true);
        ofBackground(20);
        ofSetWindowTitle("invasiv " + string(VERSION_NAME));
        gui.setup();
    }

    ofAddListener(watcher.filesChanged, this, &ofApp::onFilesChanged);

    string pPath = loadSettings();
    if (!bHeadless && (pPath == "" || !ofDirectory(pPath).exists()))
    {
        ofFileDialogResult res = ofSystemLoadDialog("Select Invasiv Project Folder", true);
        if (res.bSuccess)
        {
            pPath = res.getPath();
            saveSettings(pPath);
        }
        else
        {
            pPath = ofFilePath::getCurrentExeDir();
        }
    }
    else if (bHeadless && pPath == "") {
        pPath = ofFilePath::getCurrentExeDir();
    }

    strncpy(pathInputBuf, pPath.c_str(), 255);
    reloadProject(pPath);
}

void ofApp::saveSettings(string path)
{
    ofJson settings;
    settings["projectPath"] = path;
    ofSaveJson("settings.json", settings);
}

string ofApp::loadSettings()
{
    ofFile f("settings.json");
    if (f.exists())
    {
        ofJson settings;
        f >> settings;
        return settings.value("projectPath", "");
    }
    return "";
}

void ofApp::reloadProject(string path)
{
    projectPath = path;
    ofDirectory dir(path);
    if (!dir.exists())
        dir.create(true);

    string configsDir = ofFilePath::join(path, "configs");
    ofDirectory dConf(configsDir);
    if (!dConf.exists())
        dConf.create();

    mediaDir = ofFilePath::join(path, "media");
    ofDirectory dMedia(mediaDir);
    if (!dMedia.exists())
        dMedia.create();

    string configPath = ofFilePath::join(configsDir, "config.json");
    identity.setup(configPath);

    if (!net.isThreadRunning())
        net.setup(identity.myId, mediaDir);
    else
        net.setMediaPath(mediaDir);

    string warpPath = ofFilePath::join(configsDir, "warps.json");
    warper.metro = &metro;
    warper.setup(warpPath, mediaDir, identity.myId);

    string statesPath = ofFilePath::join(configsDir, "states.json");
    stateMgr.setup(statesPath);

    ofLogNotice("Project") << "Reloaded: " << path;
    watcher.setup(mediaDir);
}

void ofApp::onFilesChanged(std::vector<std::string> &files)
{
    warper.refreshContent();
    if (!net.isAuthority())
        return;

    ofLogNotice("MediaWatcher") << files.size() << " file(s) changed in " << mediaDir;
    for (const auto &f : files)
    {
        net.offerFile(f);
    }
}

void ofApp::syncFullState()
{
    ofJson root;
    map<string, ofJson> groups;
    for (auto &surf : warper.allSurfaces)
        groups[surf->ownerId].push_back(surf->toJson());
    for (auto &kv : groups)
        root["peers"][kv.first] = kv.second;
    net.sendStructure(root.dump());
}

void ofApp::update()
{
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

    int size = 0;
    while ((size = net.receive(packetBuffer, 65535)) > 0)
    {
        PacketHeader *h = (PacketHeader *)packetBuffer;
        if (h->id != PACKET_ID || strncmp(h->senderId, identity.myId.c_str(), 8) == 0)
            continue;

        if (h->type == PKT_HEARTBEAT)
        {
            HeartbeatPacket *p = (HeartbeatPacket *)packetBuffer;
            net.updatePeer(p->peerId, (AppRole)p->role, p->isSyncing, p->syncProgress, p->syncingFile);
        }
        else if (h->type == PKT_WARP_DATA && !net.isAuthority())
        {
            WarpPacket *p = (WarpPacket *)packetBuffer;
            warper.updatePeerPoint(p->ownerId, p->surfaceIndex, p->mode, p->pointIndex, p->x, p->y);
        }
        else if (h->type == PKT_METRONOME && !net.isAuthority())
        {
            MetronomePacket *p = (MetronomePacket *)packetBuffer;
            metro.bpm = p->bpm;
            metro.referenceTime = p->referenceTime;
            metro.beatsPerBar = p->beatsPerBar;
        }
        else if (h->type == PKT_STRUCT && !net.isAuthority())
        {
            string jStr(packetBuffer + sizeof(PacketHeader), size - sizeof(PacketHeader));
            string warpPath = ofFilePath::join(ofFilePath::join(projectPath, "configs"), "warps.json");
            ofBufferToFile(warpPath, ofBuffer(jStr.c_str(), jStr.length()));
            warper.loadJson(jStr);
        }
        else if (h->type == PKT_FILE_OFFER && !net.isAuthority())
        {
            FileOfferPacket *p = (FileOfferPacket *)packetBuffer;
            string name = string(packetBuffer + sizeof(FileOfferPacket), p->nameLen);
            string remoteHash = string(p->hash, 32);

            string fullPath = ofFilePath::join(mediaDir, name);
            string localHash = TinyMD5::getFileMD5(fullPath);

            if (localHash != remoteHash)
            {
                ofLogNotice("Network") << "Accepting File: " << name;
                incoming.active = true;
                incoming.name = name;
                incoming.total = p->totalSize;
                incoming.current = 0;
                incoming.buf.allocate(p->totalSize);
            }
        }
        else if (h->type == PKT_FILE_CHUNK && incoming.active)
        {
            FileChunkPacket *p = (FileChunkPacket *)packetBuffer;
            if (p->offset + p->size <= incoming.total)
            {
                memcpy(incoming.buf.getData() + p->offset, packetBuffer + sizeof(FileChunkPacket), p->size);
                incoming.current += p->size;
            }
        }
        else if (h->type == PKT_FILE_END && incoming.active)
        {
            incoming.active = false;
            string finalPath = ofFilePath::join(mediaDir, incoming.name);
            string tmpPath = finalPath + ".tmp";
            ofBufferToFile(tmpPath, incoming.buf);
            ofFile(tmpPath).renameTo(finalPath, true, true);
            ofLogNotice("Network") << "File transfer complete: " << incoming.name;
            warper.refreshContent();
        }
        else if (h->type == PKT_WARP_MOVE_ALL && !net.isAuthority())
        {
            WarpMoveAllPacket *p = (WarpMoveAllPacket *)packetBuffer;
            vector<shared_ptr<WarpSurface>> subset = warper.getSurfacesForPeer(p->ownerId);
            if (p->surfaceIndex < subset.size())
            {
                subset[p->surfaceIndex]->moveAll(p->dx, p->dy, p->mode);
            }
        }
        else if (h->type == PKT_WARP_SCALE_ALL && !net.isAuthority())
        {
            WarpScaleAllPacket *p = (WarpScaleAllPacket *)packetBuffer;
            vector<shared_ptr<WarpSurface>> subset = warper.getSurfacesForPeer(p->ownerId);
            if (p->surfaceIndex < subset.size())
            {
                subset[p->surfaceIndex]->scaleAll(p->scaleFactor, glm::vec2(p->centroidX, p->centroidY), p->mode);
            }
        }
    }
}

void ofApp::draw()
{
    if (bHeadless) return;

    if (net.isAuthority())
    {
        if (net.isEditing())
        {
            warper.drawDebug();
        }
        
        // Minimal status overlay for performance mode
        if (!net.isEditing()) {
             ofDrawBitmapStringHighlight("MASTER: PERFORMANCE MODE", 10, 20, ofColor::black, ofColor::green);
        }

        AppComponents components = {
            identity, net, warper, watcher, stateMgr, metro, pathInputBuf, projectPath
        };
        gui.draw(components);
    }
    else
    {
        warper.draw();
        AppRole masterRole = net.getMasterRole();
        if (masterRole == ROLE_MASTER_EDIT)
        {
            ofDrawBitmapStringHighlight("Role: PEER | ID: " + identity.myId, 10, 20);
            if (incoming.active)
            {
                float pct = (float)incoming.current / incoming.total * 100.0;
                ofDrawBitmapStringHighlight("Syncing " + incoming.name + ": " + ofToString(pct, 1) + "%", 10, 40);
                ofPushStyle();
                ofNoFill();
                ofSetColor(255);
                ofDrawRectangle(10, 50, 200, 10);
                ofFill();
                ofSetColor(0, 255, 0);
                ofDrawRectangle(10, 50, 200 * (pct / 100.0), 10);
                ofPopStyle();
            }
        }
    }
}

void ofApp::mousePressed(int x, int y, int button)
{
    if (bHeadless || !net.isEditing() || ImGui::GetIO().WantCaptureMouse)
        return;
    warper.mousePressed(x, y, net);
}

void ofApp::mouseDragged(int x, int y, int button)
{
    if (bHeadless || !net.isEditing() || ImGui::GetIO().WantCaptureMouse)
        return;
    warper.mouseDragged(x, y, net);
}

void ofApp::mouseReleased(int x, int y, int button)
{
    if (!bHeadless && net.isEditing())
        warper.mouseReleased(net);
}

void ofApp::keyPressed(int key)
{
    if (!net.isEditing()) {
        stateMgr.processKey(key, warper, net);
    }

    if (key == 'f')
    {
        identity.toggleFullscreen();
    }

    if (key == 'm') 
    {
        warper.reset();
        net.setRole(ROLE_MASTER_EDIT);
        syncFullState();
    }
    if (key == 'n') 
    {
        warper.reset();
        net.setRole(ROLE_MASTER_PERFORM);
        syncFullState();
    }
    if (key == 'p') 
    {
        warper.reset();
        warper.targetPeerId = identity.myId;
        net.setRole(ROLE_PEER);
    }
}

void ofApp::exit()
{
    ofRemoveListener(watcher.filesChanged, this, &ofApp::onFilesChanged);
}
