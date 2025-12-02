#include "ofApp.h"
#include "PacketDef.h"
#include "TinyMD5.h"
#include <filesystem>

void ofApp::setup()
{
    ofSetFrameRate(60);
    ofSetVerticalSync(true);
    ofBackground(20);
    gui.setup();
    ofAddListener(watcher.filesChanged, this, &ofApp::onFilesChanged);

    string cwd = std::filesystem::current_path().string();
    strncpy(pathInputBuf, cwd.c_str(), 255);
    reloadProject(cwd);
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
    warper.setup(warpPath, mediaDir, identity.myId);

    ofLogNotice("Project") << "Reloaded: " << path;
    watcher.setup(mediaDir);
}

void ofApp::onFilesChanged(std::vector<std::string> &files)
{
    warper.refreshContent();
    if (!net.isMaster) return;

    ofLogNotice("MediaWatcher") << files.size() << " file(s) changed in " << mediaDir;
    for (const auto &f : files)
    {
        ofLogNotice("MediaWatcher") << " - offering file: " << f;
        net.offerFile(f);
    }
}

void ofApp::syncFullState()
{
    if (!net.isMaster) return;

    ofLogNotice("Sync") << "Broadcasting full state to peers...";

    string warpPath = ofFilePath::join(ofFilePath::join(projectPath, "configs"), "warps.json");
    ofFile f(warpPath);
    if(f.exists()){
        string jsonStr = f.readToBuffer().getText();
        net.sendStructure(jsonStr);
    }

    vector<string> allFiles = watcher.getAllItems();
    for(auto& file : allFiles) {
        net.offerFile(file);
    }
}

void ofApp::update()
{
    watcher.update();
    warper.update();
    net.updatePeers();
    
    // -- NEW: Update Network thread with my current sync status --
    float pct = 0.0f;
    if(incoming.total > 0) pct = (float)incoming.current / (float)incoming.total;
    net.setLocalSyncStatus(incoming.active, incoming.name, pct);

    int size = 0;
    while ((size = net.receive(packetBuffer, 65535)) > 0)
    {
        PacketHeader *h = (PacketHeader *)packetBuffer;

        if (h->id != PACKET_ID)
            continue;

        if (strncmp(h->senderId, identity.myId.c_str(), 8) == 0) {
            continue; 
        }

        if (h->type == PKT_HEARTBEAT)
        {
            HeartbeatPacket *p = (HeartbeatPacket *)packetBuffer;
            if (string(p->peerId) == identity.myId) continue;

            bool isNew = net.peers.find(p->peerId) == net.peers.end();
            
            // -- UPDATED: Store new sync fields --
            Network::PeerData pd;
            pd.id = p->peerId;
            pd.isMaster = p->isMaster;
            pd.lastSeen = ofGetElapsedTimef();
            pd.isSyncing = p->isSyncing;
            pd.syncProgress = p->syncProgress;
            pd.syncingFile = string(p->syncingFile);
            
            net.peers[p->peerId] = pd;

            if(isNew && net.isMaster) {
                ofLogNotice("Network") << "New Peer Discovered: " << p->peerId << " -> Syncing State.";
                syncFullState();
            }
        }
        else if (h->type == PKT_WARP_DATA && !net.isMaster)
        {
            WarpPacket *p = (WarpPacket *)packetBuffer;
            warper.updatePeerPoint(p->ownerId, p->surfaceIndex, p->mode, p->pointIndex, p->x, p->y);
        }
        else if (h->type == PKT_STRUCT && !net.isMaster)
        {
            string jStr(packetBuffer + sizeof(PacketHeader), size - sizeof(PacketHeader));
            string warpPath = ofFilePath::join(ofFilePath::join(projectPath, "configs"), "warps.json");
            ofBufferToFile(warpPath, ofBuffer(jStr.c_str(), jStr.length()));
            warper.loadJson(jStr);
            ofLogNotice("Network") << "Received and applied Structure Sync";
        }
        else if (h->type == PKT_FILE_OFFER && !net.isMaster)
        {
            FileOfferPacket *p = (FileOfferPacket *)packetBuffer;
            string name = string(packetBuffer + sizeof(FileOfferPacket), p->nameLen);
            string remoteHash = string(p->hash, 32); 

            string fullPath = ofFilePath::join(mediaDir, name);
            string localHash = TinyMD5::getFileMD5(fullPath);

            if(localHash != remoteHash) {
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
    }
}

void ofApp::draw()
{
    warper.draw();

    if (net.isMaster)
    {
        warper.drawDebug();
        drawUI();
    }
    else
    {
        if (net.hasActiveMaster())
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
                ofDrawRectangle(10, 50, 200 * (pct/100.0), 10);
                ofPopStyle();
            }
        }
    }
}

void ofApp::drawUI()
{
    gui.begin();
    ImGuiStyle &style = ImGui::GetStyle();
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.8f, 0.2f, 0.3f, 1.0f);

    if (ImGui::Begin("invasiv", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (ImGui::InputText("Project Path", pathInputBuf, 256)) {}
        ImGui::SameLine();
        if (ImGui::Button("Reload")) reloadProject(string(pathInputBuf));

        ImGui::Separator();

        // -- NEW: Master File Status List --
        if (ImGui::TreeNode("Media Status")) {
            vector<string> files = watcher.getAllItems();
            if(files.empty()) ImGui::Text("No media files found.");
            
            for(const auto& f : files) {
                // Check if anyone is syncing this file
                bool anySyncing = false;
                string syncDetails = "";
                for(auto& p : net.peers) {
                    if(p.second.isSyncing && p.second.syncingFile == f) {
                        anySyncing = true;
                        syncDetails += p.first + "(" + ofToString(p.second.syncProgress * 100.0, 0) + "%) ";
                    }
                }

                if(anySyncing) {
                    ImGui::TextColored(ImVec4(1, 0.5, 0, 1), "%s [Syncing: %s]", f.c_str(), syncDetails.c_str());
                } else {
                    ImGui::TextColored(ImVec4(0, 1, 0, 1), "%s [Synced]", f.c_str());
                }
            }
            ImGui::TreePop();
        }
        
        ImGui::Separator();

        if (ImGui::TreeNode("Instances"))
        {
            // ME
            string label = "[me] " + identity.myId;
            if (ImGui::Selectable(label.c_str(), warper.targetPeerId == identity.myId)) warper.targetPeerId = identity.myId;

            // PEERS
            for (auto &p : net.peers)
            {
                string plabel = "[" + string(p.second.isMaster ? "M" : "P") + "] " + p.first;
                
                // -- NEW: Show progress in instance list --
                if(p.second.isSyncing) {
                    plabel += " [Syncing " + ofToString(p.second.syncProgress * 100.0, 0) + "%]";
                }
                
                if (ImGui::Selectable(plabel.c_str(), warper.targetPeerId == p.first))
                {
                    warper.targetPeerId = p.first;
                }
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Surfaces"))
        {
            vector<shared_ptr<WarpSurface>> subset = warper.getSurfacesForPeer(warper.targetPeerId);
            for (size_t i = 0; i < subset.size(); i++)
            {
                string sName = ofToString(i) + ": " + subset[i]->id + " [" + subset[i]->contentId + "]";
                if (ImGui::Selectable(sName.c_str(), warper.selectedIndex == (int)i))
                {
                    warper.selectedIndex = (int)i;
                }
            }
            if (ImGui::Button("+"))
                warper.addLayer(warper.targetPeerId, &net);
            ImGui::SameLine();
            if (ImGui::Button("-"))
                warper.removeLayer(warper.targetPeerId, &net);
            ImGui::TreePop();
        }

        vector<shared_ptr<WarpSurface>> subset = warper.getSurfacesForPeer(warper.targetPeerId);
        if (warper.selectedIndex < (int)subset.size())
        {
            ImGui::Text("Target: %s", warper.targetPeerId.c_str());
            ImGui::Text("Surface: %s", subset[warper.selectedIndex]->id.c_str());

            if (ImGui::Selectable("no edit", warper.editMode == EDIT_NONE))
                warper.editMode = EDIT_NONE;
            if (ImGui::Selectable("edit texture", warper.editMode == EDIT_TEXTURE))
                warper.editMode = EDIT_TEXTURE;
            if (ImGui::Selectable("edit mapping", warper.editMode == EDIT_MAPPING))
                warper.editMode = EDIT_MAPPING;

            ImGui::ColorButton("##c1", ImVec4(0.8, 0.2, 0.3, 1));
            ImGui::SameLine();
            ImGui::ColorButton("##c2", ImVec4(0.2, 0.3, 0.4, 1));
        }

        ImGui::Separator();
        if (ImGui::Button("Force Sync Content")) {
             syncFullState();
        }
    }
    ImGui::End();
    gui.end();
}

void ofApp::mousePressed(int x, int y, int button)
{
    if (!net.isMaster || ImGui::GetIO().WantCaptureMouse)
        return;
    warper.mousePressed(x, y, net);
}

void ofApp::mouseDragged(int x, int y, int button)
{
    if (!net.isMaster || ImGui::GetIO().WantCaptureMouse)
        return;
    warper.mouseDragged(x, y, net);
}

void ofApp::mouseReleased(int x, int y, int button)
{
    if (net.isMaster)
        warper.mouseReleased(net);
}

void ofApp::keyPressed(int key)
{
    if (key == 'm') {
        net.setRole(true);
        syncFullState();
    }
    if (key == 'p') {
        net.setRole(false);
    }
}

void ofApp::exit()
{
    ofRemoveListener(watcher.filesChanged, this, &ofApp::onFilesChanged);
}