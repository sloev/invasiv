#pragma once
#include "ofMain.h"
#include "WarpSurface.h"
#include "Network.h"
#include "Content.h"

class WarpController
{
public:
    vector<shared_ptr<WarpSurface>> allSurfaces;
    ContentManager contents;
    int selectedIndex = 0;
    int editMode = EDIT_MAPPING;
    glm::vec2 lastMouse;
    string savePath;
    string mediaPath;
    string myPeerId;
    string targetPeerId;

    void setup(string _savePath, string _mediaPath, string _myId);
    void refreshContent();

    vector<shared_ptr<WarpSurface>> getSurfacesForPeer(string peerId);
    vector<string> getContentList();

    void setSurfaceContent(string peerId, int surfIdx, string contentId, Network &net);

    void update();
    void draw();
    void drawDebug();

    void resizeSurface(string peerId, int surfIdx, int dRow, int dCol, Network &net);
    void swapLayerOrder(string owner, int index1, int index2, Network &net);
    void addLayer(string owner, Network *net);
    void reset();
    void removeLayer(string owner, Network *net);
    void removeLayerById(string idToRemove, Network *net);
    
    void updatePeerId(string oldId, string newId, Network &net);
    void updateSurfaceId(string oldId, string newId, Network &net);

    void mousePressed(int x, int y, Network &net);
    void mouseDragged(int x, int y, Network &net);
    void mouseReleased(Network &net);

    void sync(Network &net);
    void loadJson(string jStr);
    void updatePeerPoint(string owner, int idx, int mode, int pt, float x, float y);
};
