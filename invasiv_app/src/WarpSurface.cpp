#include "WarpSurface.h"

WarpSurface::WarpSurface(string _owner)
{
    ownerId = _owner;
    id = ofToString((int)ofRandom(1000, 9999));
    setup(1, 1);
}

void WarpSurface::setup(int r, int c)
{
    rows = std::max(1, r);
    cols = std::max(1, c);

    controlRender.resize((rows + 1) * (cols + 1));
    controlSource.resize((rows + 1) * (cols + 1));

    for (int y = 0; y <= rows; y++)
    {
        for (int x = 0; x <= cols; x++)
        {
            float px = (float)x / cols;
            float py = (float)y / rows;
            int idx = x + y * (cols + 1);
            controlRender[idx] = glm::vec3(px, py, 0);
            controlSource[idx] = glm::vec3(px, py, 0);
        }
    }
    rebuildMeshTopology();
}

void WarpSurface::setResolution(int res)
{
    if (res < 2) res = 2;
    if (res != resolution)
    {
        resolution = res;
        rebuildMeshTopology();
    }
}

void WarpSurface::setGridSize(int newRows, int newCols)
{
    if (newRows < 1) newRows = 1;
    if (newCols < 1) newCols = 1;
    if (newRows == rows && newCols == cols) return;

    vector<glm::vec3> newRender;
    vector<glm::vec3> newSource;
    newRender.resize((newRows + 1) * (newCols + 1));
    newSource.resize((newRows + 1) * (newCols + 1));

    for (int y = 0; y <= newRows; y++)
    {
        for (int x = 0; x <= newCols; x++)
        {
            float u = (float)x / (float)newCols;
            float v = (float)y / (float)newRows;
            int idx = x + y * (newCols + 1);
            newRender[idx] = getSurfacePoint(controlRender, rows, cols, u, v);
            newSource[idx] = getSurfacePoint(controlSource, rows, cols, u, v);
        }
    }

    rows = newRows;
    cols = newCols;
    controlRender = newRender;
    controlSource = newSource;
    selectedPoint = -1;
    rebuildMeshTopology();
}

void WarpSurface::rebuildMeshTopology()
{
    int resX = cols * resolution;
    int resY = rows * resolution;

    renderMesh.clear();
    sourceMesh.clear();
    renderMesh.setMode(OF_PRIMITIVE_TRIANGLES);

    int totalVerts = (resX + 1) * (resY + 1);
    renderMesh.getVertices().resize(totalVerts);
    sourceMesh.getVertices().resize(totalVerts);

    int stride = resX + 1;
    for (int y = 0; y < resY; y++)
    {
        for (int x = 0; x < resX; x++)
        {
            int p1 = x + y * stride;
            int p2 = (x + 1) + y * stride;
            int p3 = x + (y + 1) * stride;
            int p4 = (x + 1) + (y + 1) * stride;

            renderMesh.addIndex(p1);
            renderMesh.addIndex(p3);
            renderMesh.addIndex(p2);
            renderMesh.addIndex(p2);
            renderMesh.addIndex(p3);
            renderMesh.addIndex(p4);
        }
    }
    updateMeshPositions();
}

void WarpSurface::requestMeshUpdate() { meshDirty = true; }

void WarpSurface::updateMeshPositions()
{
    calculateSplineSurface(controlRender, renderMesh.getVertices(), rows, cols, resolution);
    calculateSplineSurface(controlSource, sourceMesh.getVertices(), rows, cols, resolution);
    meshDirty = false;
    lastMeshUpdate = ofGetElapsedTimef();
}

glm::vec3 WarpSurface::getSurfacePoint(const vector<glm::vec3> &ctrls, int r, int c, float u, float v)
{
    float xVal = u * c;
    float yVal = v * r;
    int xInt = (int)xVal;
    int yInt = (int)yVal;
    if (xInt >= c) xInt = c - 1;
    if (yInt >= r) yInt = r - 1;
    float tX = xVal - xInt;
    float tY = yVal - yInt;

    glm::vec3 pts[4];
    for (int k = 0; k < 4; k++)
    {
        int rowY = yInt + (k - 1);
        int idxY = std::max(0, std::min(r, rowY));
        int i0 = std::max(0, xInt - 1) + idxY * (c + 1);
        int i1 = xInt + idxY * (c + 1);
        int i2 = std::min(c, xInt + 1) + idxY * (c + 1);
        int i3 = std::min(c, xInt + 2) + idxY * (c + 1);
        pts[k] = evalCatmullRom(ctrls[i0], ctrls[i1], ctrls[i2], ctrls[i3], tX);
    }
    return evalCatmullRom(pts[0], pts[1], pts[2], pts[3], tY);
}

glm::vec3 WarpSurface::evalCatmullRom(const glm::vec3 &p0, const glm::vec3 &p1, const glm::vec3 &p2, const glm::vec3 &p3, float t)
{
    float t2 = t * t;
    float t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

void WarpSurface::calculateSplineSurface(const vector<glm::vec3> &ctrls, vector<glm::vec3> &targetVerts, int cRows, int cCols, int res)
{
    int highResW = cCols * res + 1;
    static vector<glm::vec3> tempRowVerts;
    tempRowVerts.resize((cRows + 1) * highResW);

    for (int y = 0; y <= cRows; y++)
    {
        for (int x = 0; x < cCols; x++)
        {
            int i0 = std::max(0, x - 1) + y * (cCols + 1);
            int i1 = x + y * (cCols + 1);
            int i2 = std::min(cCols, x + 1) + y * (cCols + 1);
            int i3 = std::min(cCols, x + 2) + y * (cCols + 1);
            for (int k = 0; k < res; k++)
            {
                float t = (float)k / (float)res;
                tempRowVerts[(x * res + k) + y * highResW] = evalCatmullRom(ctrls[i0], ctrls[i1], ctrls[i2], ctrls[i3], t);
            }
        }
        tempRowVerts[(cCols * res) + y * highResW] = ctrls[cCols + y * (cCols + 1)];
    }

    for (int x = 0; x < highResW; x++)
    {
        for (int y = 0; y < cRows; y++)
        {
            int stride = highResW;
            int i0 = x + std::max(0, y - 1) * stride;
            int i1 = x + y * stride;
            int i2 = x + std::min(cRows, y + 1) * stride;
            int i3 = x + std::min(cRows, y + 2) * stride;
            for (int k = 0; k < res; k++)
            {
                float t = (float)k / (float)res;
                int finalIdx = x + (y * res + k) * highResW;
                if (finalIdx < (int)targetVerts.size())
                    targetVerts[finalIdx] = evalCatmullRom(tempRowVerts[i0], tempRowVerts[i1], tempRowVerts[i2], tempRowVerts[i3], t);
            }
        }
        int finalRowIdx = x + (cRows * res) * highResW;
        if (finalRowIdx < (int)targetVerts.size())
            targetVerts[finalRowIdx] = tempRowVerts[x + cRows * highResW];
    }
}

void WarpSurface::draw(ofTexture &tex, float w, float h, bool faded)
{
    if (meshDirty && (ofGetElapsedTimef() - lastMeshUpdate > updateInterval)) updateMeshPositions();
    renderMesh.clearTexCoords();
    const auto &srcVerts = sourceMesh.getVertices();
    if (renderMesh.getTexCoords().capacity() < srcVerts.size()) renderMesh.getTexCoords().reserve(srcVerts.size());
    for (const auto &v : srcVerts) renderMesh.addTexCoord(tex.getCoordFromPercent(v.x, v.y));
    ofPushMatrix();
    ofScale(w, h, 1);
    if (faded) ofSetColor(255, 100);
    else ofSetColor(255);
    tex.bind();
    renderMesh.draw();
    tex.unbind();
    ofPopMatrix();
}

void WarpSurface::drawDebug(float w, float h, int mode)
{
    if (mode == EDIT_NONE) return;
    ofPushStyle();
    ofPushMatrix();
    ofScale(w, h, 1);
    auto &verts = (mode == EDIT_TEXTURE) ? controlSource : controlRender;
    ofSetLineWidth(2);
    ofSetColor(255, 0, 0);
    for (int y = 0; y <= rows; y++)
    {
        ofPolyline line;
        for (int x = 0; x <= cols; x++) line.addVertex(verts[x + y * (cols + 1)]);
        line.draw();
    }
    for (int x = 0; x <= cols; x++)
    {
        ofPolyline line;
        for (int y = 0; y <= rows; y++) line.addVertex(verts[x + y * (cols + 1)]);
        line.draw();
    }
    for (size_t i = 0; i < verts.size(); i++)
    {
        ofSetColor((int)i == selectedPoint ? ofColor::yellow : ofColor::cyan);
        ofDrawCircle(verts[i], (int)i == selectedPoint ? 0.015 : 0.01);
    }
    ofPopMatrix();
    ofPopStyle();
}

void WarpSurface::setContentId(string id) { contentId = id; }
string WarpSurface::getContentId() { return contentId; }

int WarpSurface::getHit(float x, float y, float w, float h, int mode)
{
    if (mode == EDIT_NONE) return -1;
    auto &verts = (mode == EDIT_TEXTURE) ? controlSource : controlRender;
    float minD = 30;
    int hit = -1;
    for (size_t i = 0; i < verts.size(); i++)
    {
        float d = ofDist(x, y, verts[i].x * w, verts[i].y * h);
        if (d < minD) { minD = d; hit = (int)i; }
    }
    return hit;
}

void WarpSurface::updatePoint(int idx, float x, float y, int mode)
{
    if (mode == EDIT_NONE) return;
    auto *target = (mode == EDIT_TEXTURE) ? &controlSource : &controlRender;
    if (target && idx >= 0 && idx < (int)target->size())
    {
        (*target)[idx] = glm::vec3(x, y, 0);
        requestMeshUpdate();
    }
}

bool WarpSurface::contains(float x, float y, float w, float h, int mode)
{
    if (mode == EDIT_NONE) return false;
    auto &verts = (mode == EDIT_TEXTURE) ? controlSource : controlRender;
    if (verts.empty()) return false;
    float minX = verts[0].x, maxX = verts[0].x, minY = verts[0].y, maxY = verts[0].y;
    for (auto &v : verts)
    {
        if (v.x < minX) minX = v.x;
        if (v.x > maxX) maxX = v.x;
        if (v.y < minY) minY = v.y;
        if (v.y > maxY) maxY = v.y;
    }
    float nx = x / w, ny = y / h;
    return (nx >= minX && nx <= maxX && ny >= minY && ny <= maxY);
}

void WarpSurface::moveAll(float dx, float dy, int mode)
{
    if (mode == EDIT_NONE) return;
    auto *verts = (mode == EDIT_TEXTURE) ? &controlSource : &controlRender;
    for (auto &v : *verts)
    {
        v.x = ofClamp(v.x + dx, 0.0f, 1.0f);
        v.y = ofClamp(v.y + dy, 0.0f, 1.0f);
    }
    requestMeshUpdate();
}

void WarpSurface::scaleAll(float scaleFactor, glm::vec2 centroid, int mode)
{
    if (mode == EDIT_NONE) return;
    auto *verts = (mode == EDIT_TEXTURE) ? &controlSource : &controlRender;
    for (auto &v : *verts)
    {
        glm::vec2 current(v.x, v.y);
        glm::vec2 dir = current - centroid;
        glm::vec2 newPos = centroid + (dir * scaleFactor);
        v.x = ofClamp(newPos.x, 0.0f, 1.0f);
        v.y = ofClamp(newPos.y, 0.0f, 1.0f);
    }
    requestMeshUpdate();
}

ofJson WarpSurface::toJson()
{
    ofJson j;
    j["content"] = contentId;
    j["rows"] = rows;
    j["cols"] = cols;
    j["res"] = resolution;
    j["id"] = id;
    j["owner"] = ownerId;
    for (auto &v : controlRender) j["geo"].push_back({{"x", v.x}, {"y", v.y}});
    for (auto &v : controlSource) j["tex"].push_back({{"x", v.x}, {"y", v.y}});
    return j;
}

void WarpSurface::fromJson(ofJson j)
{
    ownerId = j.value("owner", "unknown");
    id = j.value("id", "0000");
    contentId = j.value("content", "");
    setup(j.value("rows", 3), j.value("cols", 3));
    setResolution(j.value("res", 20));
    if (j.contains("geo"))
    {
        for (size_t i = 0; i < j["geo"].size() && i < controlRender.size(); i++)
            controlRender[i] = glm::vec3(j["geo"][i]["x"], j["geo"][i]["y"], 0);
    }
    if (j.contains("tex"))
    {
        for (size_t i = 0; i < j["tex"].size() && i < controlSource.size(); i++)
            controlSource[i] = glm::vec3(j["tex"][i]["x"], j["tex"][i]["y"], 0);
    }
    rebuildMeshTopology();
}
