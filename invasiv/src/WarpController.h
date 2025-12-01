#pragma once
#include "ofMain.h"
#include "WarpSurface.h"
#include "Network.h"

class WarpController {
public:
    vector<shared_ptr<WarpSurface>> allSurfaces;
    
    int selectedIndex = 0; 
    int editMode = EDIT_NONE;
    string savePath;
    
    string myPeerId;      
    string targetPeerId;  

    void setup(string _savePath, string _myId) {
        savePath = _savePath;
        myPeerId = _myId;
        targetPeerId = _myId; 
        
        if(ofFile(savePath).exists()) {
            loadJson(ofBufferFromFile(savePath).getText());
        }
        
        if(getSurfacesForPeer(myPeerId).empty()) {
            addLayer(myPeerId, nullptr);
        }
    }
    
    vector<shared_ptr<WarpSurface>> getSurfacesForPeer(string peerId) {
        vector<shared_ptr<WarpSurface>> subset;
        for(auto& s : allSurfaces) {
            if(s->ownerId == peerId) subset.push_back(s);
        }
        return subset;
    }

    void draw(ofTexture& tex) {
        for(auto& s : allSurfaces) {
            if(s->ownerId == myPeerId) s->draw(tex, ofGetWidth(), ofGetHeight());
        }
    }

    void drawDebug() {
        vector<shared_ptr<WarpSurface>> subset = getSurfacesForPeer(targetPeerId);
        for(auto& s : subset) {
            s->drawDebug(ofGetWidth(), ofGetHeight(), editMode);
        }
    }

    void addLayer(string owner, Network* net) {
        auto s = make_shared<WarpSurface>(owner);
        allSurfaces.push_back(s);
        vector<shared_ptr<WarpSurface>> subset = getSurfacesForPeer(owner);
        selectedIndex = (int)subset.size() - 1;
        if(net) sync(*net);
    }

    void removeLayer(string owner, Network* net) {
        vector<shared_ptr<WarpSurface>> subset = getSurfacesForPeer(owner);
        if(subset.empty()) return;
        
        if(selectedIndex >= 0 && selectedIndex < (int)subset.size()) {
            string idToRemove = subset[selectedIndex]->id;
            for(auto it = allSurfaces.begin(); it != allSurfaces.end(); ) {
                if((*it)->id == idToRemove) it = allSurfaces.erase(it);
                else ++it;
            }
            selectedIndex = max(0, selectedIndex - 1);
            if(net) sync(*net);
        }
    }

    void mousePressed(int x, int y, Network& net) {
        if(!net.isMaster) return;
        vector<shared_ptr<WarpSurface>> subset = getSurfacesForPeer(targetPeerId);
        if(selectedIndex >= 0 && selectedIndex < (int)subset.size()) {
            int hit = subset[selectedIndex]->getHit(x, y, ofGetWidth(), ofGetHeight(), editMode);
            if(hit != -1) subset[selectedIndex]->selectedPoint = hit;
        }
    }

    void mouseDragged(int x, int y, Network& net) {
        if(!net.isMaster) return;
        vector<shared_ptr<WarpSurface>> subset = getSurfacesForPeer(targetPeerId);
        if(selectedIndex >= 0 && selectedIndex < (int)subset.size()) {
            auto s = subset[selectedIndex];
            if(s->selectedPoint != -1) {
                float nx = ofClamp((float)x/ofGetWidth(), 0, 1);
                float ny = ofClamp((float)y/ofGetHeight(), 0, 1);
                s->updatePoint(s->selectedPoint, nx, ny, editMode);
                net.sendWarp(s->ownerId, selectedIndex, editMode, s->selectedPoint, nx, ny);
            }
        }
    }

    void mouseReleased(Network& net) {
        if(!net.isMaster) return;
        vector<shared_ptr<WarpSurface>> subset = getSurfacesForPeer(targetPeerId);
        if(selectedIndex >= 0 && selectedIndex < (int)subset.size()) {
            if(subset[selectedIndex]->selectedPoint != -1) {
                subset[selectedIndex]->selectedPoint = -1;
                sync(net); 
            }
        }
    }

    void sync(Network& net) {
        ofJson root;
        map<string, ofJson> groups;
        for(auto& s : allSurfaces) groups[s->ownerId].push_back(s->toJson());
        for(auto& kv : groups) root["peers"][kv.first] = kv.second;
        string jStr = root.dump();
        ofSaveJson(savePath, root);
        net.sendStructure(jStr);
    }
    
    void loadJson(string jStr) {
        try {
            ofJson root = ofJson::parse(jStr);
            allSurfaces.clear();
            if(root.contains("peers")) {
                for(auto& peerItem : root["peers"].items()) {
                    string owner = peerItem.key();
                    for(auto& layerItem : peerItem.value()) {
                        auto s = make_shared<WarpSurface>(owner);
                        s->fromJson(layerItem);
                        allSurfaces.push_back(s);
                    }
                }
            } 
            else if(root.contains("layers")) {
                for(auto& item : root["layers"]) {
                    auto s = make_shared<WarpSurface>(myPeerId);
                    s->fromJson(item);
                    allSurfaces.push_back(s);
                }
            }
            selectedIndex = 0;
        } catch(...) { ofLogError() << "JSON Parse Error"; }
    }
    
    void updatePeerPoint(string owner, int idx, int mode, int pt, float x, float y) {
        vector<shared_ptr<WarpSurface>> subset = getSurfacesForPeer(owner);
        if(idx < (int)subset.size()) {
            subset[idx]->updatePoint(pt, x, y, mode);
        }
    }
};