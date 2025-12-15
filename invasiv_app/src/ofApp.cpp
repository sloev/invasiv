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
    if (!net.isMaster)
        return;

    ofLogNotice("MediaWatcher") << files.size() << " file(s) changed in " << mediaDir;
    for (const auto &f : files)
    {
        ofLogNotice("MediaWatcher") << " - offering file: " << f;
        net.offerFile(f);
    }
}

void ofApp::syncFullState()
{
    if (!net.isMaster)
        return;

    ofLogNotice("Sync") << "Broadcasting full state to peers...";

    string warpPath = ofFilePath::join(ofFilePath::join(projectPath, "configs"), "warps.json");
    ofFile f(warpPath);
    if (f.exists())
    {
        string jsonStr = f.readToBuffer().getText();
        net.sendStructure(jsonStr);
    }

    vector<string> allFiles = watcher.getAllItems();
    for (auto &file : allFiles)
    {
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
    if (incoming.total > 0)
        pct = (float)incoming.current / (float)incoming.total;
    net.setLocalSyncStatus(incoming.active, incoming.name, pct);

    int size = 0;
    while ((size = net.receive(packetBuffer, 65535)) > 0)
    {
        PacketHeader *h = (PacketHeader *)packetBuffer;

        if (h->id != PACKET_ID)
            continue;

        if (strncmp(h->senderId, identity.myId.c_str(), 8) == 0)
        {
            continue;
        }

        if (h->type == PKT_HEARTBEAT)
        {
            HeartbeatPacket *p = (HeartbeatPacket *)packetBuffer;
            if (string(p->peerId) == identity.myId)
                continue;

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

            if (isNew && net.isMaster)
            {
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
        else if (h->type == PKT_WARP_MOVE_ALL && !net.isMaster)
        {
            WarpMoveAllPacket *p = (WarpMoveAllPacket *)packetBuffer;
            vector<shared_ptr<WarpSurface>> subset = warper.getSurfacesForPeer(p->ownerId);
            if (p->surfaceIndex < subset.size())
            {
                subset[p->surfaceIndex]->moveAll(p->dx, p->dy, p->mode);
            }
        }
        else if (h->type == PKT_WARP_SCALE_ALL && !net.isMaster)
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

    if (net.isMaster)
    {
        warper.drawDebug();
        drawUI();
    }
    else
    {
        warper.draw();

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
                ofDrawRectangle(10, 50, 200 * (pct / 100.0), 10);
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
        if (ImGui::InputText("Project Path", pathInputBuf, 256))
        {
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload"))
            reloadProject(string(pathInputBuf));

        ImGui::Separator();

        // -- NEW: Master File Status List --
        if (ImGui::TreeNode("Media Status"))
        {
            vector<string> files = watcher.getAllItems();
            if (files.empty())
                ImGui::Text("No media files found.");

            for (const auto &f : files)
            {
                // Check if anyone is syncing this file
                bool anySyncing = false;
                string syncDetails = "";
                for (auto &p : net.peers)
                {
                    if (p.second.isSyncing && p.second.syncingFile == f)
                    {
                        anySyncing = true;
                        syncDetails += p.first + "(" + ofToString(p.second.syncProgress * 100.0, 0) + "%) ";
                    }
                }

                if (anySyncing)
                {
                    ImGui::TextColored(ImVec4(1, 0.5, 0, 1), "%s [Syncing: %s]", f.c_str(), syncDetails.c_str());
                }
                else
                {
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
            if (ImGui::Selectable(label.c_str(), warper.targetPeerId == identity.myId))
                warper.targetPeerId = identity.myId;

            // PEERS
            for (auto &p : net.peers)
            {
                string plabel = "[" + string(p.second.isMaster ? "M" : "P") + "] " + p.first;

                // -- NEW: Show progress in instance list --
                if (p.second.isSyncing)
                {
                    plabel += " [Syncing " + ofToString(p.second.syncProgress * 100.0, 0) + "%]";
                }

                if (ImGui::Selectable(plabel.c_str(), warper.targetPeerId == p.first))
                {
                    warper.targetPeerId = p.first;
                }
            }
            ImGui::TreePop();
        }
        ImGui::SeparatorText("Surfaces");
        ImGui::SameLine();
        if (ImGui::Button("ADD"))
            warper.addLayer(warper.targetPeerId, &net);

        vector<shared_ptr<WarpSurface>> subset = warper.getSurfacesForPeer(warper.targetPeerId);

        for (int i = 0; i < (int)subset.size(); i++)
        {
            string sName = ofToString(i) + ": " + subset[i]->id + " [" + subset[i]->contentId + "]";

            // 1. Draw the Selectable
            if (ImGui::Selectable(sName.c_str(), warper.selectedIndex == i, ImGuiSelectableFlags_AllowItemOverlap))
            {
                warper.selectedIndex = i;
            }

            // 2. Handle Reordering (Active + Dragged + Not Hovered)
            // Only allow reordering if we are the master
            if (net.isMaster && ImGui::IsItemActive() && !ImGui::IsItemHovered())
            {
                int i_next = i + (ImGui::GetMouseDragDelta(0).y < 0.f ? -1 : 1);

                if (i_next >= 0 && i_next < (int)subset.size())
                {
                    warper.swapLayerOrder(warper.targetPeerId, i, i_next, net);
                    ImGui::ResetMouseDragDelta();
                }
            }
            ImGui::SameLine();
            ImGui::PushID(i);

            if (ImGui::SmallButton("DELETE"))
            {
                warper.removeLayerById(subset[i]->id, &net);
            }
            ImGui::PopID();
        }
        ImGui::SeparatorText("Surface Settings");

        if (warper.selectedIndex < (int)subset.size())
        {
            ImGui::Text("Target: %s", warper.targetPeerId.c_str());
            ImGui::Text("Surface: %s", subset[warper.selectedIndex]->id.c_str());

            // 1. Get List and Current ID
            vector<string> items = warper.getContentList();
            string currentId = subset[warper.selectedIndex]->contentId;

            // 2. Determine "Preview Value"
            // If the current ID exists in the list, show it.
            // If it DOESN'T exist (file deleted?), show it anyway so the user knows what was there.
            const char *previewValue = currentId.c_str();

            // 3. BeginCombo (Method 1)
            ImGui::Text("Content: ");
            ImGui::SameLine();

            if (ImGui::BeginCombo("##", previewValue))
            {
                for (int n = 0; n < items.size(); n++)
                {
                    bool is_selected = (currentId == items[n]);

                    // Display the item
                    if (ImGui::Selectable(items[n].c_str(), is_selected))
                    {
                        // Update network if clicked
                        warper.setSurfaceContent(warper.targetPeerId, warper.selectedIndex, items[n], net);
                    }

                    // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            // if (ImGui::Selectable("no edit", warper.editMode == EDIT_NONE))
            //     warper.editMode = EDIT_NONE;
            if (ImGui::Selectable("edit texture", warper.editMode == EDIT_TEXTURE))
                warper.editMode = EDIT_TEXTURE;
            if (ImGui::Selectable("edit mapping", warper.editMode == EDIT_MAPPING))
                warper.editMode = EDIT_MAPPING;

            if (warper.editMode != EDIT_NONE)
            {
                ImGui::Separator();

                auto s = subset[warper.selectedIndex];

                int uiRows = s->rows - 1;
                ImGui::Text("Rows: %d", uiRows);
                float spacing = ImGui::GetStyle().ItemInnerSpacing.x;

                ImGui::SameLine();
                ImGui::PushID("add-remove rows");

                if (ImGui::Button("-"))
                {
                    warper.resizeSurface(warper.targetPeerId, warper.selectedIndex, -1, 0, net);
                }
                ImGui::SameLine(0.0f, spacing);

                if (ImGui::Button("+"))
                {
                    warper.resizeSurface(warper.targetPeerId, warper.selectedIndex, 1, 0, net);
                }
                ImGui::PopID();

                ImGui::Separator();

                int uiCols = s->cols - 1;
                ImGui::Text("Cols: %d", uiCols);
                ImGui::SameLine();
                ImGui::PushID("add-remove cols");

                if (ImGui::Button("-"))
                {
                    warper.resizeSurface(warper.targetPeerId, warper.selectedIndex, 0, -1, net);
                }
                ImGui::SameLine(0.0f, spacing);

                if (ImGui::Button("+"))
                {
                    warper.resizeSurface(warper.targetPeerId, warper.selectedIndex, 0, 1, net);
                }
                ImGui::PopID();
            }
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
    if (key == 'f')
    {
        identity.toggleFullscreen();
    }
    if (key == 'm')
    {
        warper.reset();
        net.setRole(true);
        syncFullState();
    }
    if (key == 'p')
    {
        warper.reset();
        warper.targetPeerId = identity.myId;
        net.setRole(false);
    }
}

void ofApp::exit()
{
    ofRemoveListener(watcher.filesChanged, this, &ofApp::onFilesChanged);
}