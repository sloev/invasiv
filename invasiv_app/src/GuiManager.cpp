#include "GuiManager.h"

void GuiManager::setup() {
    gui.setup();
}

void GuiManager::draw(Identity &identity, Network &net, WarpController &warper, MediaWatcher &watcher, StateManager &stateMgr, char* pathInputBuf, string &projectPath) {
    if (net.isAuthority()) {
        if (net.isEditing()) {
            drawEditingUI(identity, net, warper, watcher, stateMgr, pathInputBuf, projectPath);
        } else {
            drawPerformUi(identity, net, warper, watcher, stateMgr);
        }
    }
}

void GuiManager::drawPerformUi(Identity &identity, Network &net, WarpController &warper, MediaWatcher &watcher, StateManager &stateMgr)
{
    gui.begin();
    ImGui::SetNextWindowSizeConstraints(ImVec2(800, 600), ImVec2(FLT_MAX, FLT_MAX));

    if (ImGui::Begin("Invasiv Performance", nullptr))
    {
        if (ImGui::BeginTable("MainLayoutTable", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Instances", ImGuiTableColumnFlags_WidthStretch, 0.3f);
            ImGui::TableSetupColumn("Sequencing", ImGuiTableColumnFlags_WidthStretch, 0.4f);
            ImGui::TableSetupColumn("Settings", ImGuiTableColumnFlags_WidthStretch, 0.3f);
            
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
            ImGui::AlignTextToFramePadding();
            ImGui::Text("STATES");
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 40);
            if(ImGui::SmallButton("NEW##State")) {
                string name = "State " + ofToString(stateMgr.states.size() + 1);
                stateMgr.saveState(name, warper);
            }
            ImGui::Separator();

            if (ImGui::CollapsingHeader("Saved States", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (ImGui::BeginTable("StatesTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
                {
                    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 30.0f);
                    ImGui::TableSetupColumn("State Title");
                    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                    ImGui::TableHeadersRow();

                    for (int i = 0; i < (int)stateMgr.states.size(); i++)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::Text("%d", i);
                        ImGui::TableNextColumn();
                        bool isCurrent = (stateMgr.currentStateIndex == i);
                        if(ImGui::Selectable(stateMgr.states[i].name.c_str(), isCurrent, ImGuiSelectableFlags_SpanAllColumns)) {
                            stateMgr.applyState(i, warper, net);
                        }
                        ImGui::TableNextColumn();
                        ImGui::PushID(i);
                        if(ImGui::SmallButton("DEL")) {
                            stateMgr.removeState(i);
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            }

            ImGui::Dummy(ImVec2(0, 20));
            ImGui::AlignTextToFramePadding();
            ImGui::Text("TRIGGERS");
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 40);
            
            static int selectedStateForTrigger = 0;
            static char keyInput[2] = "";

            if(ImGui::SmallButton("NEW##Trig")) {
                if(keyInput[0] != '\0' && !stateMgr.states.empty()) {
                    stateMgr.addTrigger(keyInput[0], selectedStateForTrigger);
                    keyInput[0] = '\0';
                }
            }
            ImGui::Separator();

            if (ImGui::CollapsingHeader("Input Mapping", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Text("Add Key Trigger:");
                ImGui::SetNextItemWidth(40);
                ImGui::InputText("Key", keyInput, 2);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(120);
                if(ImGui::BeginCombo("State", stateMgr.states.empty() ? "No States" : stateMgr.states[selectedStateForTrigger].name.c_str())) {
                    for(int n=0; n < (int)stateMgr.states.size(); n++) {
                        if(ImGui::Selectable(stateMgr.states[n].name.c_str(), selectedStateForTrigger == n))
                            selectedStateForTrigger = n;
                    }
                    ImGui::EndCombo();
                }

                if (ImGui::BeginTable("TriggersTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
                {
                    ImGui::TableSetupColumn("State Target");
                    ImGui::TableSetupColumn("Inputs (Key)");
                    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                    ImGui::TableHeadersRow();

                    for (int i = 0; i < (int)stateMgr.triggers.size(); i++)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        int sIdx = stateMgr.triggers[i].stateIndex;
                        string sName = (sIdx >= 0 && sIdx < (int)stateMgr.states.size()) ? stateMgr.states[sIdx].name : "Unknown";
                        ImGui::Text("%s", sName.c_str());
                        
                        ImGui::TableNextColumn();
                        char kChar = (char)stateMgr.triggers[i].key;
                        ImGui::Text("Key '%c'", kChar);

                        ImGui::TableNextColumn();
                        ImGui::PushID(i);
                        if(ImGui::SmallButton("DEL")) {
                            stateMgr.removeTrigger(i);
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            }

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
    gui.end(); 
}

void GuiManager::drawEditingUI(Identity &identity, Network &net, WarpController &warper, MediaWatcher &watcher, StateManager &stateMgr, char* pathInputBuf, string &projectPath)
{
    gui.begin();
    ImGuiStyle &style = ImGui::GetStyle();

    if (net.isEditing())
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.8f, 0.2f, 0.3f, 1.0f);
    else
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.2f, 0.6f, 0.3f, 1.0f);

    if (ImGui::Begin("invasiv", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::InputText("Project Path", pathInputBuf, 256);
        ImGui::SameLine();
        if (ImGui::Button("Reload")) {
            // This is a bit tricky, ofApp should handle this. 
            // We'll see how to callback or handle this later.
            // For now, let's just keep the UI.
        }

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
            if (lastIdOwner != identity.myId) {
                strncpy(idInputBuf, identity.myId.c_str(), 63);
                lastIdOwner = identity.myId;
            }
            
            if (ImGui::InputText("My ID", idInputBuf, 64, ImGuiInputTextFlags_EnterReturnsTrue)) {
                string newId = string(idInputBuf);
                if (newId != identity.myId && newId != "") {
                    string oldId = identity.myId;
                    identity.myId = newId;
                    identity.save();
                    net.myId = newId;
                    warper.updatePeerId(oldId, newId, net);
                    lastIdOwner = newId;
                }
            }

            string label = "[me] " + identity.myId;
            if (ImGui::Selectable(label.c_str(), warper.targetPeerId == identity.myId))
                warper.targetPeerId = identity.myId;

            for (auto &p : net.peers)
            {
                string rStr = "P";
                if (p.second.role == ROLE_MASTER_EDIT) rStr = "M(Edit)";
                if (p.second.role == ROLE_MASTER_PERFORM) rStr = "M(Perf)";

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
            auto currentSurface = subset[warper.selectedIndex];
            if (lastSurfaceId != currentSurface->id) {
                strncpy(surfaceIdInputBuf, currentSurface->id.c_str(), 63);
                lastSurfaceId = currentSurface->id;
            }

            ImGui::Text("Target: %s", warper.targetPeerId.c_str());
            if (ImGui::InputText("Surface ID", surfaceIdInputBuf, 64, ImGuiInputTextFlags_EnterReturnsTrue)) {
                string newSId = string(surfaceIdInputBuf);
                if (newSId != currentSurface->id && newSId != "") {
                    warper.updateSurfaceId(currentSurface->id, newSId, net);
                    lastSurfaceId = newSId;
                }
            }

            vector<string> items = warper.getContentList();
            string currentId = subset[warper.selectedIndex]->contentId;
            const char *previewValue = currentId.c_str();

            ImGui::Text("Content: ");
            ImGui::SameLine();

            if (ImGui::BeginCombo("##", previewValue))
            {
                for (int n = 0; n < (int)items.size(); n++)
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
                if (ImGui::Button("-")) warper.resizeSurface(warper.targetPeerId, warper.selectedIndex, -1, 0, net);
                ImGui::SameLine(0.0f, spacing);
                if (ImGui::Button("+")) warper.resizeSurface(warper.targetPeerId, warper.selectedIndex, 1, 0, net);
                ImGui::PopID();

                ImGui::Separator();
                int uiCols = s->cols - 1;
                ImGui::Text("Cols: %d", uiCols);
                ImGui::SameLine();
                ImGui::PushID("add-remove cols");
                if (ImGui::Button("-")) warper.resizeSurface(warper.targetPeerId, warper.selectedIndex, 0, -1, net);
                ImGui::SameLine(0.0f, spacing);
                if (ImGui::Button("+")) warper.resizeSurface(warper.targetPeerId, warper.selectedIndex, 0, 1, net);
                ImGui::PopID();
            }
        }
    }
    ImGui::End();
    gui.end();
}
