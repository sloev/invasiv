#ifndef OF_BILINEAR_WARP_H
#define OF_BILINEAR_WARP_H

#include "ofMain.h"

class ofBilinearWarp {
private:
    std::vector<ofVec2f> inputPoints;
    std::vector<ofVec2f> outputPoints;
    int numCols;
    int numRows;
    ofMesh mesh;
    bool dirty;
    float lastTexW, lastTexH, lastOutW, lastOutH;

    ofVec2f interpolate(const std::vector<ofVec2f>& grid, float u, float v, int cols, int rows) const {
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

    void rebuildMesh(float texW, float texH, float outW, float outH) {
        mesh.clear();
        mesh.setMode(OF_PRIMITIVE_TRIANGLES);

        for (int r = 0; r < numRows; ++r) {
            for (int c = 0; c < numCols; ++c) {
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

        for (int r = 0; r < numRows - 1; ++r) {
            for (int c = 0; c < numCols - 1; ++c) {
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
    ofBilinearWarp() : numCols(2), numRows(2), dirty(true), lastTexW(0), lastTexH(0), lastOutW(0), lastOutH(0) {
        inputPoints.resize(4);
        inputPoints[0].set(0.0f, 0.0f);
        inputPoints[1].set(1.0f, 0.0f);
        inputPoints[2].set(0.0f, 1.0f);
        inputPoints[3].set(1.0f, 1.0f);
        outputPoints = inputPoints;
    }

    int getDivX() const { return numCols - 1; }
    int getDivY() const { return numRows - 1; }

    void setDivisions(int divX, int divY) {
        int newCols = divX + 1;
        int newRows = divY + 1;
        if (newCols < 2 || newRows < 2) return; // Minimum 1 division (2x2 points)

        // Resample input points
        std::vector<ofVec2f> newInput(newRows * newCols);
        for (int r = 0; r < newRows; ++r) {
            float v = static_cast<float>(r) / (newRows - 1);
            for (int c = 0; c < newCols; ++c) {
                float u = static_cast<float>(c) / (newCols - 1);
                newInput[r * newCols + c] = interpolate(inputPoints, u, v, numCols, numRows);
            }
        }
        inputPoints = std::move(newInput);

        // Resample output points
        std::vector<ofVec2f> newOutput(newRows * newCols);
        for (int r = 0; r < newRows; ++r) {
            float v = static_cast<float>(r) / (newRows - 1);
            for (int c = 0; c < newCols; ++c) {
                float u = static_cast<float>(c) / (newCols - 1);
                newOutput[r * newCols + c] = interpolate(outputPoints, u, v, numCols, numRows);
            }
        }
        outputPoints = std::move(newOutput);

        numCols = newCols;
        numRows = newRows;
        dirty = true;
    }

    void addDivisionX() {
        setDivisions(getDivX() + 1, getDivY());
    }

    void addDivisionY() {
        setDivisions(getDivX(), getDivY() + 1);
    }

    void removeDivisionX() {
        int newDivX = getDivX() - 1;
        if (newDivX >= 1) setDivisions(newDivX, getDivY());
    }

    void removeDivisionY() {
        int newDivY = getDivY() - 1;
        if (newDivY >= 1) setDivisions(getDivX(), newDivY);
    }

    ofVec2f getInputPoint(int col, int row) const {
        if (col < 0 || col >= numCols || row < 0 || row >= numRows) return ofVec2f(0, 0);
        return inputPoints[row * numCols + col];
    }

    void setInputPoint(int col, int row, const ofVec2f& p) {
        if (col < 0 || col >= numCols || row < 0 || row >= numRows) return;
        inputPoints[row * numCols + col] = p;
        dirty = true;
    }

    ofVec2f getOutputPoint(int col, int row) const {
        if (col < 0 || col >= numCols || row < 0 || row >= numRows) return ofVec2f(0, 0);
        return outputPoints[row * numCols + col];
    }

    void setOutputPoint(int col, int row, const ofVec2f& p) {
        if (col < 0 || col >= numCols || row < 0 || row >= numRows) return;
        outputPoints[row * numCols + col] = p;
        dirty = true;
    }

    void draw(const ofTexture& tex) {
        float texW = tex.getWidth();
        float texH = tex.getHeight();
        float outW = ofGetWidth();
        float outH = ofGetHeight();
        if (dirty || texW != lastTexW || texH != lastTexH || outW != lastOutW || outH != lastOutH) {
            rebuildMesh(texW, texH, outW, outH);
        }
        tex.bind();
        mesh.draw();
        tex.unbind();
    }

    ofJson toJson() const {
        ofJson j;
        j["divX"] = getDivX();
        j["divY"] = getDivY();

        ofJson ips = ofJson::array();
        for (const auto& p : inputPoints) {
            ofJson pp = ofJson::array();
            pp.push_back(p.x);
            pp.push_back(p.y);
            ips.push_back(pp);
        }
        j["inputPoints"] = ips;

        ofJson ops = ofJson::array();
        for (const auto& p : outputPoints) {
            ofJson pp = ofJson::array();
            pp.push_back(p.x);
            pp.push_back(p.y);
            ops.push_back(pp);
        }
        j["outputPoints"] = ops;

        return j;
    }

    void fromJson(const ofJson& j) {
        int divX = j.value("divX", 1);
        int divY = j.value("divY", 1);
        setDivisions(divX, divY); // Initializes grids to uniform

        auto ips = j.value("inputPoints", ofJson::array());
        if (ips.size() == inputPoints.size()) {
            for (size_t i = 0; i < ips.size(); ++i) {
                inputPoints[i].x = ips[i][0];
                inputPoints[i].y = ips[i][1];
            }
        }

        auto ops = j.value("outputPoints", ofJson::array());
        if (ops.size() == outputPoints.size()) {
            for (size_t i = 0; i < ops.size(); ++i) {
                outputPoints[i].x = ops[i][0];
                outputPoints[i].y = ops[i][1];
            }
        }

        dirty = true;
    }
};

class ofWarpStack {
private:
    std::vector<ofBilinearWarp> warps;

public:
    ofWarpStack() {}

    size_t addWarp() {
        warps.emplace_back();
        return warps.size() - 1;
    }

    void removeWarp(size_t index) {
        if (index < warps.size()) {
            warps.erase(warps.begin() + index);
        }
    }

    ofBilinearWarp& getWarp(size_t index) {
        return warps[index];
    }

    const ofBilinearWarp& getWarp(size_t index) const {
        return warps[index];
    }

    size_t getNumWarps() const {
        return warps.size();
    }

    // Draw all warps in order using the same texture (user can override per draw if needed)
    void drawAll(const ofTexture& tex) {
        for (auto& warp : warps) {
            warp.draw(tex);
        }
    }

    ofJson toJson() const {
        ofJson j;
        ofJson warr = ofJson::array();
        for (const auto& w : warps) {
            warr.push_back(w.toJson());
        }
        j["warps"] = warr;
        return j;
    }

    void fromJson(const ofJson& j) {
        warps.clear();
        auto warr = j.value("warps", ofJson::array());
        for (const auto& wj : warr) {
            ofBilinearWarp w;
            w.fromJson(wj);
            warps.push_back(w);
        }
    }

    void saveToFile(const std::string& path) const {
        ofSaveJson(path, toJson());
    }

    void loadFromFile(const std::string& path) {
        fromJson(ofLoadJson(path));
    }
    // Inside ofWarpStack class definition
void drawControlPoints(
    size_t warpIndex,
    bool editingInput = true,
    int selectedCol = -1,
    int selectedRow = -1,
    float selectionRadius = 0.03f,
    float pointSize = 7.0f
) const {
    if (warpIndex >= warps.size()) return;
    const ofBilinearWarp& warp = warps[warpIndex];

    float outW = ofGetWidth();
    float outH = ofGetHeight();

    ofSetLineWidth(1.5);
    ofNoFill();

    // Draw grid lines in output space
    ofSetColor(255, 100);
    for (int r = 0; r < warp.getDivY() + 1; ++r) {
        ofBeginShape();
        for (int c = 0; c < warp.getDivX() + 1; ++c) {
            ofVec2f p = warp.getOutputPoint(c, r);
            ofVertex(p.x * outW, p.y * outH);
        }
        ofEndShape();
    }
    for (int c = 0; c < warp.getDivX() + 1; ++c) {
        ofBeginShape();
        for (int r = 0; r < warp.getDivY() + 1; ++r) {
            ofVec2f p = warp.getOutputPoint(c, r);
            ofVertex(p.x * outW, p.y * outH);
        }
        ofEndShape();
    }

    // Draw control points
    for (int r = 0; r < warp.getDivY() + 1; ++r) {
        for (int c = 0; c < warp.getDivX() + 1; ++c) {
            ofVec2f op = warp.getOutputPoint(c, r);
            float px = op.x * outW;
            float py = op.y * outH;

            if (c == selectedCol && r == selectedRow) {
                ofSetColor(255, 200, 0);
                ofFill();
                ofDrawCircle(px, py, pointSize + 3);
                ofSetColor(0);
                ofDrawCircle(px, py, pointSize - 1);
            } else {
                ofSetColor(editingInput ? ofColor(100, 200, 255) : ofColor(255, 200, 100));
                ofFill();
                ofDrawCircle(px, py, pointSize);
            }

            ofSetColor(255, 150);
            ofDrawBitmapString(ofToString(c) + "," + ofToString(r), px + 10, py);
        }
    }
}
};

#endif // OF_BILINEAR_WARP_H