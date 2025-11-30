#define OF_USING_LAMBDA_LISTENERS // ← REQUIRED

#include "ofMain.h"
#include "uid.h"
#include <algorithm>
#include <unordered_set>
#include "textures.h"
#include <filesystem>
#include "ofxImGui.h"
#include "Coms.h"

#ifndef OF_BILINEAR_WARP_H
#define OF_BILINEAR_WARP_H

class ofBilinearWarp
{
private:
    std::vector<ofVec2f> inputPoints;
    std::vector<ofVec2f> outputPoints;
    int numCols;
    int numRows;
    string warpId;
    string textureId = "test";
    ofMesh mesh;
    bool dirty;
    float lastTexW, lastTexH, lastOutW, lastOutH;

    ofVec2f interpolate(const std::vector<ofVec2f> &grid, float u, float v, int cols, int rows) const
    {
        u = ofClamp(u, 0.0f, 1.0f);
        v = ofClamp(v, 0.0f, 1.0f);
        float colf = u * (cols - 1);
        int col1 = (int)std::floor(colf);
        int col2 = std::min(col1 + 1, cols - 1);
        float rowf = v * (rows - 1);
        int row1 = (int)std::floor(rowf);
        int row2 = std::min(row1 + 1, rows - 1);
        float uu = colf - col1;
        float vv = rowf - row1;

        ofVec2f p11 = grid[row1 * cols + col1];
        ofVec2f p12 = grid[row1 * cols + col2];
        ofVec2f p21 = grid[row2 * cols + col1];
        ofVec2f p22 = grid[row2 * cols + col2];

        ofVec2f p1 = p11 * (1.0f - uu) + p12 * uu;
        ofVec2f p2 = p21 * (1.0f - uu) + p22 * uu;
        return p1 * (1.0f - vv) + p2 * vv;
    }

    void rebuildMesh(float texW, float texH, float outW, float outH)
    {
        mesh.clear();
        mesh.setMode(OF_PRIMITIVE_TRIANGLES);

        for (int r = 0; r < numRows; ++r)
        {
            for (int c = 0; c < numCols; ++c)
            {
                ofVec2f vert = getOutputPoint(c, r);
                vert.x *= outW;
                vert.y *= outH;
                ofVec3f v(vert.x, vert.y, 0.0f); // ofMesh uses ofVec3f for vertices
                mesh.addVertex(v);

                ofVec2f tc = getInputPoint(c, r);
                tc.x *= texW;
                tc.y *= texH;
                mesh.addTexCoord(tc);
            }
        }

        for (int r = 0; r < numRows - 1; ++r)
        {
            for (int c = 0; c < numCols - 1; ++c)
            {
                int i1 = r * numCols + c;
                int i2 = r * numCols + c + 1;
                int i3 = (r + 1) * numCols + c + 1;
                int i4 = (r + 1) * numCols + c;

                // Triangle 1
                mesh.addIndex(i1);
                mesh.addIndex(i2);
                mesh.addIndex(i4);

                // Triangle 2
                mesh.addIndex(i2);
                mesh.addIndex(i3);
                mesh.addIndex(i4);
            }
        }

        dirty = false;
        lastTexW = texW;
        lastTexH = texH;
        lastOutW = outW;
        lastOutH = outH;
    }

public:
    ofBilinearWarp() : numCols(2), numRows(2), dirty(true), lastTexW(0), lastTexH(0), lastOutW(0), lastOutH(0)
    {
        inputPoints.resize(4);
        inputPoints[0].set(0.0f, 0.0f);
        inputPoints[1].set(1.0f, 0.0f);
        inputPoints[2].set(0.0f, 1.0f);
        inputPoints[3].set(1.0f, 1.0f);
        outputPoints = inputPoints;
    }

    int getDivX() const { return numCols - 1; }
    int getDivY() const { return numRows - 1; }

    int getNumCols() const { return numCols; }
    int getNumRows() const { return numRows; }

    void setDivisions(int divX, int divY)
    {
        int newCols = divX + 1;
        int newRows = divY + 1;
        if (newCols < 2 || newRows < 2)
            return; // Minimum 1 division (2x2 points)

        // Resample input points
        std::vector<ofVec2f> newInput(newRows * newCols);
        for (int r = 0; r < newRows; ++r)
        {
            float v = static_cast<float>(r) / (newRows - 1);
            for (int c = 0; c < newCols; ++c)
            {
                float u = static_cast<float>(c) / (newCols - 1);
                newInput[r * newCols + c] = interpolate(inputPoints, u, v, numCols, numRows);
            }
        }
        inputPoints = std::move(newInput);

        // Resample output points
        std::vector<ofVec2f> newOutput(newRows * newCols);
        for (int r = 0; r < newRows; ++r)
        {
            float v = static_cast<float>(r) / (newRows - 1);
            for (int c = 0; c < newCols; ++c)
            {
                float u = static_cast<float>(c) / (newCols - 1);
                newOutput[r * newCols + c] = interpolate(outputPoints, u, v, numCols, numRows);
            }
        }
        outputPoints = std::move(newOutput);

        numCols = newCols;
        numRows = newRows;
        dirty = true;
    }

    void addDivisionX()
    {
        setDivisions(getDivX() + 1, getDivY());
    }

    void addDivisionY()
    {
        setDivisions(getDivX(), getDivY() + 1);
    }

    void removeDivisionX()
    {
        int newDivX = getDivX() - 1;
        if (newDivX >= 1)
            setDivisions(newDivX, getDivY());
    }

    void removeDivisionY()
    {
        int newDivY = getDivY() - 1;
        if (newDivY >= 1)
            setDivisions(getDivX(), newDivY);
    }

    ofVec2f getInputPoint(int col, int row) const
    {
        if (col < 0 || col >= numCols || row < 0 || row >= numRows)
            return ofVec2f(0, 0);
        return inputPoints[row * numCols + col];
    }

    void setInputPoint(int col, int row, const ofVec2f &p)
    {
        if (col < 0 || col >= numCols || row < 0 || row >= numRows)
            return;
        inputPoints[row * numCols + col] = p;
        dirty = true;
    }

    ofVec2f getOutputPoint(int col, int row) const
    {
        if (col < 0 || col >= numCols || row < 0 || row >= numRows)
            return ofVec2f(0, 0);
        return outputPoints[row * numCols + col];
    }

    void setOutputPoint(int col, int row, const ofVec2f &p)
    {
        if (col < 0 || col >= numCols || row < 0 || row >= numRows)
            return;
        outputPoints[row * numCols + col] = p;
        dirty = true;
    }
    void setWarpId(string id)
    {
        warpId = id;
    }

    string getWarpId() const
    {
        return warpId;
    }

    void setTextureId(string id)
    {
        textureId = id;
    }
    string getTextureId() const
    {
        return textureId;
    }
    void draw(TextureManager &textureManager)
    {
        ofTexture tex = textureManager.getTextureById(getTextureId());
        float texW = tex.getWidth();
        float texH = tex.getHeight();
        float outW = ofGetWidth();
        float outH = ofGetHeight();
        if (dirty || texW != lastTexW || texH != lastTexH || outW != lastOutW || outH != lastOutH)
        {
            rebuildMesh(texW, texH, outW, outH);
        }
        tex.bind();
        mesh.draw();
        tex.unbind();
    }
    void drawTexture(TextureManager &textureManager)
    {
        ofTexture tex = textureManager.getTextureById(getTextureId());

        float outW = ofGetWidth();
        float outH = ofGetHeight();
        tex.draw(0.0, 0.0, outW, outH);
    }

    ofJson toJson() const
    {
        ofJson j;
        ofJson ds = ofJson::array();
        ds.push_back(getDivX());
        ds.push_back(getDivY());
        j["d"] = ds;
        ofJson ps = ofJson::array();
        for (size_t i = 0; i < inputPoints.size(); i++)
        {
            const auto &ip = inputPoints[i];
            const auto &op = outputPoints[i];
            ofJson pp = ofJson::array();
            pp.push_back(ip.x);
            pp.push_back(ip.y);
            pp.push_back(op.x);
            pp.push_back(op.y);
            ps.push_back(pp);
        }
        j["p"] = ps;

        j["i"] = getWarpId();
        j["t"] = getTextureId();

        return j;
    }

    void fromJson(const ofJson &j)
    {
        string wId = j.value("i", "null");
        setWarpId(wId);
        string tId = j.value("t", "test");
        setTextureId(tId);
        auto ds = j.value("d", ofJson::array());
        int divX = ds[0];
        int divY = ds[1];
        setDivisions(divX, divY); // Initializes grids to uniform

        auto ps = j.value("p", ofJson::array());
        if (ps.size() == inputPoints.size()) // setdivisions made sure our ponts size are corrent, this checks if not correct
        {
            for (size_t i = 0; i < ps.size(); ++i)
            {
                inputPoints[i].x = ps[i][0];
                inputPoints[i].y = ps[i][1];
                outputPoints[i].x = ps[i][2];
                outputPoints[i].y = ps[i][3];
            }
        }

        dirty = true;
    }
};

#define EDIT_MODE_NONE "0"
#define EDIT_MODE_TEXTURE "1"
#define EDIT_MODE_MAPPING "2"

class ofWarpStack
{
private:
    std::vector<string> layerOrder;
    map<string, ofBilinearWarp> warps = {};
    string selectedWarpId;
    int selectedPointIndex = 0;
    string editMode = EDIT_MODE_NONE;
    mutable bool dirty = false;

public:
    ofWarpStack() {}

    ofBilinearWarp &addWarp()
    {
        ofBilinearWarp w;

        string wId = short_uid::generate8();
        w.setWarpId(wId);
        warps[wId] = w;
        layerOrder.push_back(wId);
        return warps[wId]; // ← Also fix return: reference the inserted value directly
    }

    void removeWarp(string wId)
    {
        auto it = warps.find(wId);
        if (it != warps.end())
        {
            warps.erase(it);
            layerOrder.erase(std::remove(layerOrder.begin(), layerOrder.end(), wId), layerOrder.end());
        }
    }

    ofBilinearWarp &getWarp(string wId)
    {
        return warps.at(wId); // ← Use at() for non-const ref; add ;
    }

    const ofBilinearWarp &getWarp(string wId) const
    {

        return warps.at(wId); // ← Use at() for non-const ref; add ;
    }

    size_t getNumWarps() const
    {
        return layerOrder.size();
    }
    void drawGui(Peer p)
    {

        if (ImGui::TreeNode("Info"))
        {
            if (ImGui::BeginTable("tableInfo", 4))
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("IP");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", p.ip.c_str());

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("SyncPort");

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d", p.syncPort);

                ImGui::EndTable();
            }
            ImGui::TreePop();
        }

        ImGui::SeparatorText("Surfaces");
        ImGui::PushItemFlag(ImGuiItemFlags_AllowDuplicateId, true);

        std::vector<std::string> layernames = layerOrder; // make a copy to modify
        if (ImGui::BeginTable("tableSurfaceInfo", 2))
        {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);

            for (std::size_t n = 0; n < layernames.size(); ++n)
            {
                // Unique boolean for each item (required!)
                bool selected = false;
                string n_id = layernames[n];
                string buttonText;

                if (selectedWarpId == n_id)
                {
                    buttonText = std::format("[x] {}: {}", n, n_id);
                }
                else
                {
                    buttonText = std::format("[ ] {}: {}", n, n_id);
                }
                // Use the version that allows selection state

                if (ImGui::Selectable(buttonText.c_str(), &selected))
                {
                    // Optional: clicked (not dragged)
                    // You can react to actual clicks here if needed
                    if (selectedWarpId == n_id)
                    {
                        selectedWarpId = "";
                    }
                    else
                    {
                        selectedWarpId = n_id;
                    }
                }

                // Drag to reorder logic
                if (ImGui::IsItemActive() && !ImGui::IsItemHovered())
                {
                    int direction = (ImGui::GetMouseDragDelta(0).y < 0.f) ? -1 : 1;
                    int n_next = n + direction;

                    if (n_next >= 0 && n_next < (int)layernames.size())
                    {
                        std::swap(layernames[n], layernames[n_next]);
                        ImGui::ResetMouseDragDelta();
                    }
                }
            }

            // Apply changes back if needed
            layerOrder = layernames;

            ImGui::TableSetColumnIndex(1);
            auto it = warps.find(selectedWarpId);
            if (it != warps.end())
            {
                ofBilinearWarp selectedWarp = warps[selectedWarpId];
                ImGui::Text("selected warp: %s", selectedWarpId.c_str());

                if (ImGui::Selectable("no edit", editMode == EDIT_MODE_NONE))
                {
                    editMode = EDIT_MODE_NONE;
                }
                if (ImGui::Selectable("edit texture", editMode == EDIT_MODE_TEXTURE))
                {
                    editMode = EDIT_MODE_TEXTURE;
                }
                if (ImGui::Selectable("edit mapping", editMode == EDIT_MODE_MAPPING))
                {
                    editMode = EDIT_MODE_MAPPING;
                }

                int dx = selectedWarp.getNumCols();
                int dy = selectedWarp.getNumRows();
                if (ImGui::BeginTable("editPoints", dx * dy))
                {
                    for (int y = 0; y < dy; y++)
                    {
                        ImGui::TableNextRow();

                        for (int x = 0; x < dx; x++)
                        {
                            ImGui::TableSetColumnIndex(x);
                            ImGui::PushID(std::format("point {}:{}", x, y).c_str());

                            ImGui::RadioButton("", &selectedPointIndex, (y * dy) + x);
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::EndTable();
        }

        ImGui::PopItemFlag();
    }

    // Draw all warps in order using the same texture (user can override per draw if needed)
    void draw(TextureManager &textureManager)
    {
        for (string wId : layerOrder)
        {
            warps[wId].draw(textureManager);
        }
    }

    ofJson toJson() const
    {
        ofJson j;
        ofJson warr = ofJson::array();
        for (string wId : layerOrder)
        {
            const ofBilinearWarp &warp = warps.at(wId); // ← Use at(); now used below

            warr.push_back(warp.toJson());
        }
        j["w"] = warr;
        return j;
    }

    void fromJson(const ofJson &j)
    {
        layerOrder.clear();
        std::unordered_set<std::string> keepSet;

        auto warr = j.value("w", ofJson::array());
        for (const auto &wj : warr)
        {
            string wId = wj.value("i", "null");
            layerOrder.push_back(wId);
            keepSet.insert(wId); // one insert → O(1) average

            auto it = warps.find(wId);
            if (it != warps.end())
            {
                warps[wId].fromJson(wj);
            }
            else
            {
                ofBilinearWarp w;
                w.fromJson(wj);
                warps[wId] = w;
            }
        }

        // Erase all map entries whose key is NOT in the set
        std::erase_if(warps, [&keepSet](const auto &pair)
                      { return keepSet.find(pair.first) == keepSet.end(); });
        selectedPointIndex = 0;
        selectedWarpId = "";
    }

    bool isDirty()
    {
        return dirty;
    }

    void saveToFile(const std::string &path) const
    {
        ofSaveJson(path, toJson());
        dirty = false;
    }

    void loadFromFile(const std::string &path)
    {

        try
        {
            fromJson(ofLoadJson(path));
        }
        catch (...)
        {
            cout << "path does not exist to loadfromjson, creating now: " << path << "\n";
            ofSaveJson(path, toJson());
            fromJson(ofLoadJson(path));
        }
    }

    void drawEditmode(
        TextureManager &textureManager,
        float selectionRadius = 0.03f,
        float pointSize = 7.0f)
    {
        if (selectedWarpId != "" && editMode == EDIT_MODE_TEXTURE)
        {
            warps[selectedWarpId].drawTexture(textureManager);
        }
        else if (selectedWarpId != "" && editMode == EDIT_MODE_MAPPING)
        {
            warps[selectedWarpId].draw(textureManager);
        }
        else
        {
            draw(textureManager);
        }

        auto it = warps.find(selectedWarpId);
        if (it == warps.end() || selectedWarpId.empty())
            return;

        ofBilinearWarp &warp = warps.at(selectedWarpId); // non-const reference so we can modify

        float outW = ofGetWidth();
        float outH = ofGetHeight();

        int mouseX = ofGetMouseX();
        int mouseY = ofGetMouseY();
        bool mousePressed = ofGetMousePressed(OF_MOUSE_BUTTON_LEFT);

        // Normalized mouse position
        ofVec2f mouseNorm(mouseX / outW, mouseY / outH);

        int bestIndex = -1;
        float bestDist = selectionRadius;

        // Find closest point within radius
        int cols = warp.getNumCols();
        int rows = warp.getNumRows();

        for (int r = 0; r < rows; ++r)
        {
            for (int c = 0; c < cols; ++c)
            {
                int idx = r * cols + c;
                ofVec2f p = (editMode == EDIT_MODE_TEXTURE)
                                ? warp.getInputPoint(c, r)
                                : warp.getOutputPoint(c, r);

                float dist = p.distance(mouseNorm);
                if (dist < bestDist)
                {
                    bestDist = dist;
                    bestIndex = idx;
                }
            }
        }

        // If mouse is pressed and close enough → drag that point
        if (mousePressed && bestIndex != -1)
        {
            ofVec2f newPos = mouseNorm;
            newPos.x = ofClamp(newPos.x, 0.0f, 1.0f);
            newPos.y = ofClamp(newPos.y, 0.0f, 1.0f);

            int c = bestIndex % cols;
            int r = bestIndex / cols;

            if (editMode == EDIT_MODE_TEXTURE)
            {
                warp.setInputPoint(c, r, newPos);
                dirty = true;
            }
            else
            {
                warp.setOutputPoint(c, r, newPos);
                dirty = true;
            }

            // Optional: auto-select the point you're dragging
            selectedPointIndex = bestIndex;
        }

        // === Drawing (unchanged, just cleaned up a bit) ===
        ofSetLineWidth(1.5);
        ofNoFill();

        for (int r = 0; r < rows; ++r)
        {
            for (int c = 0; c < cols; ++c)
            {
                int idx = r * cols + c;
                ofVec2f op = (editMode == EDIT_MODE_TEXTURE)
                                 ? warp.getInputPoint(c, r)
                                 : warp.getOutputPoint(c, r);

                float px = op.x * outW;
                float py = op.y * outH;

                bool isSelected = (selectedPointIndex == idx);
                bool isHovered = (bestIndex == idx && bestDist < selectionRadius);

                if (isSelected)
                {
                    ofSetColor(255, 200, 0);
                    ofFill();
                    ofDrawCircle(px, py, pointSize + 3);
                    ofSetColor(0);
                    ofDrawCircle(px, py, pointSize - 1);
                }
                else if (isHovered)
                {
                    ofSetColor(255, 255, 100);
                    ofFill();
                    ofDrawCircle(px, py, pointSize + 2);
                }
                else
                {
                    ofSetColor(ofColor(100, 200, 255));
                    ofFill();
                    ofDrawCircle(px, py, pointSize);
                }

                ofSetColor(255, 150);
                ofDrawBitmapString(std::format("{},{}\n({},{})", c, r, px, py), px + 10, py);
            }
        }

        ofNoFill();
        ofSetColor(255);
    }
};

#endif // OF_BILINEAR_WARP_H