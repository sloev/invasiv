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

    // -- UPDATED: Check Authority instead of just isMaster --
    if (!net.isAuthority())
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
    // -- UPDATED: Check Authority --
    if (!net.isAuthority())
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

            Network::PeerData pd;
            pd.id = p->peerId;
            // -- UPDATED: Get Role --
            pd.role = (AppRole)p->role;

            pd.lastSeen = ofGetElapsedTimef();
            pd.isSyncing = p->isSyncing;
            pd.syncProgress = p->syncProgress;
            pd.syncingFile = string(p->syncingFile);

            net.peers[p->peerId] = pd;

            if (isNew && net.isAuthority())
            {
                ofLogNotice("Network") << "New Peer Discovered: " << p->peerId << " -> Syncing State.";
                syncFullState();
            }
        }
        else if (h->type == PKT_WARP_DATA && !net.isAuthority())
        {
            WarpPacket *p = (WarpPacket *)packetBuffer;
            warper.updatePeerPoint(p->ownerId, p->surfaceIndex, p->mode, p->pointIndex, p->x, p->y);
        }
        else if (h->type == PKT_STRUCT && !net.isAuthority())
        {
            string jStr(packetBuffer + sizeof(PacketHeader), size - sizeof(PacketHeader));
            string warpPath = ofFilePath::join(ofFilePath::join(projectPath, "configs"), "warps.json");
            ofBufferToFile(warpPath, ofBuffer(jStr.c_str(), jStr.length()));
            warper.loadJson(jStr);
            ofLogNotice("Network") << "Received and applied Structure Sync";
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
    // -- UPDATED: Role-based Drawing --

    // 1. I AM AN AUTHORITY (Master)
    if (net.isAuthority())
    {
        if (net.isEditing())
        {
            // Edit Mode: Show all debug info
            warper.drawDebug();
            drawEditingUI();
        }
        else
        {
            // Perform Mode: Show clean content + UI
            drawPerformUi();

            // Minimal indicator for operator
        }
    }
    // 2. I AM A PEER
    else
    {
        warper.draw();

        // Check the master's state
        AppRole masterRole = net.getMasterRole();

        // Only show debug/ID overlays if the master is in EDIT mode.
        // If master is in PERFORM mode, we show nothing (clean output).
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
void ofApp::drawPerformUi()
{
    // 1. Start the ImGui Frame
    gui.begin();

    // Set a window size constraint
    ImGui::SetNextWindowSizeConstraints(ImVec2(800, 600), ImVec2(FLT_MAX, FLT_MAX));

    if (ImGui::Begin("Invasiv Performance", nullptr))
    {
        // Create a 3-column table
        if (ImGui::BeginTable("MainLayoutTable", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Instances", ImGuiTableColumnFlags_WidthStretch, 0.3f);
            ImGui::TableSetupColumn("Sequencing", ImGuiTableColumnFlags_WidthStretch, 0.4f);
            ImGui::TableSetupColumn("Settings", ImGuiTableColumnFlags_WidthStretch, 0.3f);
            
            // ---------------------------------------------------------
            // COLUMN 1: INSTANCES & SURFACES
            // ---------------------------------------------------------
            ImGui::TableNextColumn();
            
            ImGui::TextDisabled("INSTANCES");
            ImGui::Separator();
            
            struct InstanceRef { string id; bool isMe; };
            vector<InstanceRef> instances;
            instances.push_back({identity.myId, true});
            for(auto &p : net.peers) instances.push_back({p.first, false});

            for (auto &inst : instances)
            {
                ImGui::PushID(inst.id.c_str());

                string headerName = (inst.isMe ? "[Me] " : "[Peer] ") + inst.id;
                
                if (ImGui::CollapsingHeader(headerName.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    vector<shared_ptr<WarpSurface>> surfaces = warper.getSurfacesForPeer(inst.id);
                    
                    if(surfaces.empty()) {
                        ImGui::TextDisabled("No surfaces found.");
                    } else {
                        float windowVisibleX2 = ImGui::GetWindowPos().x + ImGui::GetContentRegionAvail().x;
                        float buttonSz = 60.0f; 
                        ImGuiStyle& style = ImGui::GetStyle();

                        for (int i = 0; i < (int)surfaces.size(); i++)
                        {
                            ImGui::PushID(i);
                            
                            bool isSelected = (i % 2 == 0); 

                            if(isSelected) {
                                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.8f, 0.6f, 0.5f)); 
                                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.2f, 0.9f, 0.2f, 1.0f));
                                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
                            }

                            if (ImGui::Button(surfaces[i]->id.c_str(), ImVec2(buttonSz, buttonSz)))
                            {
                                ofLogNotice("UI") << "Clicked surface " << surfaces[i]->id;
                            }

                            if(isSelected) {
                                ImGui::PopStyleVar();
                                ImGui::PopStyleColor(2);
                            }

                            float lastButtonX2 = ImGui::GetItemRectMax().x;
                            float nextButtonX2 = lastButtonX2 + style.ItemSpacing.x + buttonSz;
                            if (i + 1 < (int)surfaces.size() && nextButtonX2 < windowVisibleX2)
                                ImGui::SameLine();
                            
                            ImGui::PopID();
                        }
                    }
                }
                ImGui::PopID();
                ImGui::Dummy(ImVec2(0, 10));
            }

            // ---------------------------------------------------------
            // COLUMN 2: MEDIA BANK, STATES, TRIGGERS
            // ---------------------------------------------------------
            ImGui::TableNextColumn();
            
            ImGui::TextDisabled("MEDIABANK");
            ImGui::Separator();
            
            if (ImGui::CollapsingHeader("Media Files", ImGuiTreeNodeFlags_DefaultOpen))
            {
                vector<string> files = watcher.getAllItems();
                if(files.empty()) ImGui::TextDisabled("No media.");

                float windowVisibleX2 = ImGui::GetWindowPos().x + ImGui::GetContentRegionAvail().x;
                float itemSz = 70.0f; 
                ImGuiStyle& style = ImGui::GetStyle();

                for (int i = 0; i < (int)files.size(); i++)
                {
                    ImGui::PushID(i);
                    if (ImGui::Button(files[i].c_str(), ImVec2(itemSz, itemSz))) {
                    }
                    
                    float lastButtonX2 = ImGui::GetItemRectMax().x;
                    float nextButtonX2 = lastButtonX2 + style.ItemSpacing.x + itemSz;
                    if (i + 1 < (int)files.size() && nextButtonX2 < windowVisibleX2)
                        ImGui::SameLine();
                    
                    ImGui::PopID();
                }
            }

            ImGui::Dummy(ImVec2(0, 20));

            // --- STATES ---
            ImGui::AlignTextToFramePadding();
            ImGui::Text("STATES");
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 40);
            if(ImGui::SmallButton("NEW##State")) {
            }
            ImGui::Separator();

            if (ImGui::CollapsingHeader("Saved States", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (ImGui::BeginTable("StatesTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
                {
                    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 30.0f);
                    ImGui::TableSetupColumn("State Title");
                    ImGui::TableHeadersRow();

                    struct MockState { int id; string title; };
                    static vector<MockState> mockStates = { {1, "Intro Scene"}, {2, "Blackout"}, {3, "Glitch Storm"} };

                    for (auto &s : mockStates)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::Text("%d", s.id);
                        ImGui::TableNextColumn();
                        if(ImGui::Selectable(s.title.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                        }
                    }
                    ImGui::EndTable();
                }
            }

            ImGui::Dummy(ImVec2(0, 20));

            // --- TRIGGERS ---
            ImGui::AlignTextToFramePadding();
            ImGui::Text("TRIGGERS");
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 40);
            if(ImGui::SmallButton("NEW##Trig")) {
            }
            ImGui::Separator();

            if (ImGui::CollapsingHeader("Input Mapping", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (ImGui::BeginTable("TriggersTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
                {
                    ImGui::TableSetupColumn("State Target");
                    ImGui::TableSetupColumn("Inputs (Key/Midi/OSC)");
                    ImGui::TableHeadersRow();

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::Text("Intro Scene");
                    ImGui::TableNextColumn(); ImGui::Text("Key '1', MIDI CC 42");
                    
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::Text("Blackout");
                    ImGui::TableNextColumn(); ImGui::Text("Spacebar");

                    ImGui::EndTable();
                }
            }

            // ---------------------------------------------------------
            // COLUMN 3: SETTINGS (AUDIO & FX)
            // ---------------------------------------------------------
            ImGui::TableNextColumn();

            if (ImGui::CollapsingHeader("Audio Input Settings", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Dummy(ImVec2(0,5));
                static float gain = 0.5f;
                static int deviceId = 0;
                static bool listen = true;
                
                ImGui::Combo("Device", &deviceId, "Default Input\0USB Mic\0Virtual Cable\0");
                ImGui::SliderFloat("Gain", &gain, 0.0f, 1.0f);
                ImGui::Checkbox("Monitor / Listen", &listen);
                
                ImGui::PlotHistogram("Levels", [](void*, int idx) -> float { 
                    return sin(idx * 0.1f) * 0.5f + 0.5f; 
                }, NULL, 50, 0, NULL, 0.0f, 1.0f, ImVec2(0, 60));
                
                ImGui::Dummy(ImVec2(0,10));
            }

            ImGui::Separator();

            if (ImGui::CollapsingHeader("Surface Effects", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Dummy(ImVec2(0,5));
                static bool fxEnable = false;
                static float colorRGB[3] = {1.0f, 1.0f, 1.0f};
                static float noiseSpeed = 0.2f;

                ImGui::Checkbox("Enable Global FX", &fxEnable);
                if(fxEnable)
                {
                    ImGui::ColorEdit3("Tint", colorRGB);
                    ImGui::SliderFloat("Noise", &noiseSpeed, 0.0f, 5.0f);
                    
                    ImGui::SeparatorText("Glitch Params");
                    static float glitchAmt = 0.0f;
                    ImGui::SliderFloat("Amount", &glitchAmt, 0.0f, 1.0f);
                }
                else
                {
                    ImGui::TextDisabled("Effects disabled.");
                }
            }

            ImGui::EndTable();
        }
    }
    ImGui::End();

    // 2. End the ImGui Frame (Render)
    gui.end(); 
}

void ofApp::drawEditingUI()
{
    gui.begin();
    ImGuiStyle &style = ImGui::GetStyle();

    // Color coding the title bar for quick visual status
    if (net.isEditing())
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.8f, 0.2f, 0.3f, 1.0f); // Red for Edit
    else
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.2f, 0.6f, 0.3f, 1.0f); // Green for Perform

    if (ImGui::Begin("invasiv", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        if (ImGui::InputText("Project Path", pathInputBuf, 256))
        {
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload"))
            reloadProject(string(pathInputBuf));

        ImGui::Separator();

        if (ImGui::TreeNode("Media Status"))
        {
            vector<string> files = watcher.getAllItems();
            if (files.empty())
                ImGui::Text("No media files found.");

            for (const auto &f : files)
            {
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
                // -- UPDATED: Show Role String --
                string rStr = "P";
                if (p.second.role == ROLE_MASTER_EDIT)
                    rStr = "M(Edit)";
                if (p.second.role == ROLE_MASTER_PERFORM)
                    rStr = "M(Perf)";

                string plabel = "[" + rStr + "] " + p.first;

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

            if (ImGui::Selectable(sName.c_str(), warper.selectedIndex == i, ImGuiSelectableFlags_AllowItemOverlap))
            {
                warper.selectedIndex = i;
            }

            if (net.isAuthority() && ImGui::IsItemActive() && !ImGui::IsItemHovered())
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

            vector<string> items = warper.getContentList();
            string currentId = subset[warper.selectedIndex]->contentId;
            const char *previewValue = currentId.c_str();

            ImGui::Text("Content: ");
            ImGui::SameLine();

            if (ImGui::BeginCombo("##", previewValue))
            {
                for (int n = 0; n < items.size(); n++)
                {
                    bool is_selected = (currentId == items[n]);

                    if (ImGui::Selectable(items[n].c_str(), is_selected))
                    {
                        warper.setSurfaceContent(warper.targetPeerId, warper.selectedIndex, items[n], net);
                    }

                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

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
    // -- UPDATED: Only pass input if Editing --
    if (!net.isEditing() || ImGui::GetIO().WantCaptureMouse)
        return;
    warper.mousePressed(x, y, net);
}

void ofApp::mouseDragged(int x, int y, int button)
{
    if (!net.isEditing() || ImGui::GetIO().WantCaptureMouse)
        return;
    warper.mouseDragged(x, y, net);
}

void ofApp::mouseReleased(int x, int y, int button)
{
    if (net.isEditing())
        warper.mouseReleased(net);
}

void ofApp::keyPressed(int key)
{
    if (key == 'f')
    {
        identity.toggleFullscreen();
    }

    // -- NEW: State Switching --
    if (key == 'm') // Master Edit
    {
        warper.reset();
        net.setRole(ROLE_MASTER_EDIT);
        syncFullState();
    }
    if (key == 'n') // Master Performance
    {
        warper.reset();
        net.setRole(ROLE_MASTER_PERFORM);
        syncFullState();
    }
    if (key == 'p') // Peer
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