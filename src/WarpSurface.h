#pragma once
#include "ofMain.h"
#include "PacketDef.h"
#include <algorithm>

class WarpSurface
{
public:
    string id;
    string ownerId;
    string contentId;

    ofVboMesh renderMesh;
    ofMesh sourceMesh;

    vector<glm::vec3> controlRender;
    vector<glm::vec3> controlSource;

    int rows = 3;
    int cols = 3;
    int resolution = 20; 

    int selectedPoint = -1;

    float lastMeshUpdate = 0.0f;
    bool meshDirty = true;
    float updateInterval = 0.1f;

    WarpSurface(string _owner);
    void setup(int r, int c);
    void setResolution(int res);
    void setGridSize(int newRows, int newCols);
    void rebuildMeshTopology();
    void requestMeshUpdate();
    void updateMeshPositions();

    glm::vec3 getSurfacePoint(const vector<glm::vec3> &ctrls, int r, int c, float u, float v);
    glm::vec3 evalCatmullRom(const glm::vec3 &p0, const glm::vec3 &p1, const glm::vec3 &p2, const glm::vec3 &p3, float t);
    void calculateSplineSurface(const vector<glm::vec3> &ctrls, vector<glm::vec3> &targetVerts, int cRows, int cCols, int res);

    void draw(ofTexture &tex, float w, float h, bool faded = false);
    void drawDebug(float w, float h, int mode);

    void setContentId(string id);
    string getContentId();

    int getHit(float x, float y, float w, float h, int mode);
    void updatePoint(int idx, float x, float y, int mode);
    bool contains(float x, float y, float w, float h, int mode);
    void moveAll(float dx, float dy, int mode);
    void scaleAll(float scaleFactor, glm::vec2 centroid, int mode);

    ofJson toJson();
    void fromJson(ofJson j);
};
