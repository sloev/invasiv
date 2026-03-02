#include "WarpController.h"

void WarpController::setup(string _savePath, string _mediaPath, string _myId)
{
    savePath = _savePath;
    mediaPath = _mediaPath;
    myPeerId = _myId;
    targetPeerId = _myId;

    contents.setup();
    contents.setMetronome(metro);
    contents.refreshMedia(mediaPath);

    if (ofFile(savePath).exists())
        loadJson(ofBufferFromFile(savePath).getText());
    if (getSurfacesForPeer(myPeerId).empty())
        addLayer(myPeerId, nullptr);
}

void WarpController::refreshContent() { 
    contents.setMetronome(metro);
    contents.refreshMedia(mediaPath); 
}

vector<shared_ptr<WarpSurface>> WarpController::getSurfacesForPeer(string peerId)
{
    vector<shared_ptr<WarpSurface>> subset;
    for (auto &s : allSurfaces)
        if (s->ownerId == peerId)
            subset.push_back(s);
    return subset;
}

vector<string> WarpController::getContentList()
{
    return contents.getContentNames();
}

void WarpController::setSurfaceContent(string peerId, int surfIdx, string contentId, Network &net)
{
    vector<shared_ptr<WarpSurface>> subset = getSurfacesForPeer(peerId);
    if (surfIdx >= 0 && surfIdx < (int)subset.size())
    {
        subset[surfIdx]->setContentId(contentId);
        sync(net);
    }
}

void WarpController::update() { contents.update(); }

void WarpController::draw()
{
    vector<shared_ptr<WarpSurface>> subset = getSurfacesForPeer(targetPeerId);

    for (size_t i = 0; i < subset.size(); i++)
    {
        ofTexture &tex = contents.getTextureById(subset[i]->contentId);
        subset[i]->draw(tex, ofGetWidth(), ofGetHeight());
    }
}

void WarpController::drawDebug()
{
    vector<shared_ptr<WarpSurface>> subset = getSurfacesForPeer(targetPeerId);
    for (size_t i = 0; i < subset.size(); i++)
    {
        ofTexture &tex = contents.getTextureById(subset[i]->contentId);
        if (editMode == EDIT_TEXTURE)
        {
            if (selectedIndex == i)
            {
                ofSetColor(255);
                tex.draw(0, 0, ofGetWidth(), ofGetHeight());
            }
        }
        else if (editMode == EDIT_MAPPING)
        {
            bool faded = selectedIndex != i;
            subset[i]->draw(tex, ofGetWidth(), ofGetHeight(), faded);
        }
    }

    if (editMode != EDIT_NONE && selectedIndex >= 0 && selectedIndex < (int)subset.size())
    {
        subset[selectedIndex]->drawDebug(ofGetWidth(), ofGetHeight(), editMode);
    }
}

void WarpController::resizeSurface(string peerId, int surfIdx, int dRow, int dCol, Network &net)
{
    vector<shared_ptr<WarpSurface>> subset = getSurfacesForPeer(peerId);
    if (surfIdx >= 0 && surfIdx < (int)subset.size())
    {
        auto s = subset[surfIdx];
        int nr = s->rows + dRow;
        int nc = s->cols + dCol;

        if (nr < 1) nr = 1;
        if (nc < 1) nc = 1;

        s->setGridSize(nr, nc);
        s->selectedPoint = -1;

        sync(net);
    }
}

void WarpController::swapLayerOrder(string owner, int index1, int index2, Network &net)
{
    vector<shared_ptr<WarpSurface>> subset = getSurfacesForPeer(owner);

    if (index1 < 0 || index1 >= (int)subset.size() || index2 < 0 || index2 >= (int)subset.size())
        return;

    auto it1 = std::find(allSurfaces.begin(), allSurfaces.end(), subset[index1]);
    auto it2 = std::find(allSurfaces.begin(), allSurfaces.end(), subset[index2]);

    if (it1 != allSurfaces.end() && it2 != allSurfaces.end())
    {
        std::iter_swap(it1, it2);

        if (selectedIndex == index1)
            selectedIndex = index2;
        else if (selectedIndex == index2)
            selectedIndex = index1;

        sync(net);
    }
}

void WarpController::addLayer(string owner, Network *net)
{
    auto s = make_shared<WarpSurface>(owner);
    allSurfaces.push_back(s);
    vector<shared_ptr<WarpSurface>> subset = getSurfacesForPeer(owner);
    selectedIndex = (int)subset.size() - 1;
    if (net)
        sync(*net);
}

void WarpController::reset()
{
    selectedIndex = 0;
    editMode = EDIT_MAPPING;
}

void WarpController::removeLayer(string owner, Network *net)
{
    vector<shared_ptr<WarpSurface>> subset = getSurfacesForPeer(owner);
    if (subset.empty())
        return;
    if (selectedIndex >= 0 && selectedIndex < (int)subset.size())
    {
        string idToRemove = subset[selectedIndex]->id;
        removeLayerById(idToRemove, net);
    }
}

void WarpController::removeLayerById(string idToRemove, Network *net)
{
    for (auto it = allSurfaces.begin(); it != allSurfaces.end();)
    {
        if ((*it)->id == idToRemove)
            it = allSurfaces.erase(it);
        else
            ++it;
    }
    selectedIndex = max(0, selectedIndex - 1);
    if (net)
        sync(*net);
}

void WarpController::updatePeerId(string oldId, string newId, Network &net)
{
    if (oldId == newId || newId == "") return;
    
    for (auto &s : allSurfaces) {
        if (s->ownerId == oldId) {
            s->ownerId = newId;
        }
    }
    
    if (myPeerId == oldId) myPeerId = newId;
    if (targetPeerId == oldId) targetPeerId = newId;
    
    sync(net);
}

void WarpController::updateSurfaceId(string oldId, string newId, Network &net)
{
    if (oldId == newId || newId == "") return;
    
    for (auto &s : allSurfaces) {
        if (s->id == oldId) {
            s->id = newId;
        }
    }
    sync(net);
}

void WarpController::mousePressed(int x, int y, Network &net)
{
    if (!net.isEditing())
        return;
        
    lastMouse = glm::vec2(x, y);

    vector<shared_ptr<WarpSurface>> subset = getSurfacesForPeer(targetPeerId);
    if (selectedIndex >= 0 && selectedIndex < (int)subset.size())
    {
        auto s = subset[selectedIndex];

        int hit = s->getHit(x, y, ofGetWidth(), ofGetHeight(), editMode);
        if (hit != -1)
        {
            s->selectedPoint = hit;
        }
        else if ((ofGetKeyPressed(OF_KEY_SHIFT) || ofGetKeyPressed(OF_KEY_ALT)) &&
                 s->contains(x, y, ofGetWidth(), ofGetHeight(), editMode))
        {
            s->selectedPoint = -2;
        }
    }
}

void WarpController::mouseDragged(int x, int y, Network &net)
{
    if (!net.isEditing())
        return;

    vector<shared_ptr<WarpSurface>> subset = getSurfacesForPeer(targetPeerId);
    if (selectedIndex >= 0 && selectedIndex < (int)subset.size())
    {
        auto s = subset[selectedIndex];

        if (s->selectedPoint != -1)
        {
            float dx = (x - lastMouse.x) / (float)ofGetWidth();
            float dy = (y - lastMouse.y) / (float)ofGetHeight();

            if (ofGetKeyPressed(OF_KEY_SHIFT))
            {
                s->moveAll(dx, dy, editMode);
                net.sendWarpMoveAll(s->ownerId, selectedIndex, editMode, dx, dy);
            }
            else if (ofGetKeyPressed(OF_KEY_ALT))
            {
                auto &verts = (editMode == EDIT_TEXTURE) ? s->sourceMesh.getVertices() : s->renderMesh.getVertices();
                glm::vec2 centroid(0, 0);
                for (auto &v : verts)
                    centroid += glm::vec2(v.x, v.y);
                if (!verts.empty())
                    centroid /= (float)verts.size();

                float scaleFactor = 1.0f + (dx + dy);
                s->scaleAll(scaleFactor, centroid, editMode);
                net.sendWarpScaleAll(s->ownerId, selectedIndex, editMode, scaleFactor, centroid.x, centroid.y);
            }
            else if (s->selectedPoint >= 0)
            {
                float nx = ofClamp((float)x / ofGetWidth(), 0, 1);
                float ny = ofClamp((float)y / ofGetHeight(), 0, 1);
                s->updatePoint(s->selectedPoint, nx, ny, editMode);
                net.sendWarp(s->ownerId, selectedIndex, editMode, s->selectedPoint, nx, ny);
            }
        }
    }
    lastMouse = glm::vec2(x, y);
}

void WarpController::mouseReleased(Network &net)
{
    if (!net.isEditing())
        return;
        
    vector<shared_ptr<WarpSurface>> subset = getSurfacesForPeer(targetPeerId);
    if (selectedIndex >= 0 && selectedIndex < (int)subset.size())
    {
        if (subset[selectedIndex]->selectedPoint != -1)
        {
            subset[selectedIndex]->selectedPoint = -1;
            sync(net);
        }
    }
}

void WarpController::sync(Network &net)
{
    ofJson root;
    map<string, ofJson> groups;
    for (auto &s : allSurfaces)
        groups[s->ownerId].push_back(s->toJson());
    for (auto &kv : groups)
        root["peers"][kv.first] = kv.second;
    string jStr = root.dump();
    ofSaveJson(savePath, root);
    net.sendStructure(jStr);
}

void WarpController::loadJson(string jStr)
{
    try
    {
        ofJson root = ofJson::parse(jStr);
        allSurfaces.clear();
        if (root.contains("peers"))
        {
            for (auto &peerItem : root["peers"].items())
            {
                string owner = peerItem.key();
                for (auto &layerItem : peerItem.value())
                {
                    auto s = make_shared<WarpSurface>(owner);
                    s->fromJson(layerItem);
                    allSurfaces.push_back(s);
                }
            }
        }
        else if (root.contains("layers"))
        {
            for (auto &item : root["layers"])
            {
                auto s = make_shared<WarpSurface>(myPeerId);
                s->fromJson(item);
                allSurfaces.push_back(s);
            }
        }
        selectedIndex = 0;
    }
    catch (...)
    {
        ofLogError() << "JSON Parse Error";
    }
}

void WarpController::updatePeerPoint(string owner, int idx, int mode, int pt, float x, float y)
{
    vector<shared_ptr<WarpSurface>> subset = getSurfacesForPeer(owner);
    if (idx < (int)subset.size())
        subset[idx]->updatePoint(pt, x, y, mode);
}
