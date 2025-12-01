#pragma once
#include "ofMain.h"
#include "PacketDef.h"

class WarpSurface {
public:
    string id;
    string ownerId;
    ofVboMesh renderMesh; 
    ofMesh sourceMesh; 
    
    int rows = 3;
    int cols = 3;
    int selectedPoint = -1;
    
    WarpSurface(string _owner) {
        ownerId = _owner;
        id = ofToString((int)ofRandom(1000, 9999));
        setup(3, 3);
    }

    void setup(int r, int c) {
        rows = r; cols = c;
        renderMesh.clear(); sourceMesh.clear();
        renderMesh.setMode(OF_PRIMITIVE_TRIANGLES);
        
        for(int y=0; y<=rows; y++) {
            for(int x=0; x<=cols; x++) {
                float px = (float)x / cols;
                float py = (float)y / rows;
                renderMesh.addVertex(ofVec3f(px, py, 0));
                sourceMesh.addVertex(ofVec3f(px, py, 0));
            }
        }
        
        for(int y=0; y<rows; y++) {
            for(int x=0; x<cols; x++) {
                int p1 = x + y*(cols+1);
                int p2 = (x+1) + y*(cols+1);
                int p3 = x + (y+1)*(cols+1);
                int p4 = (x+1) + (y+1)*(cols+1);
                renderMesh.addIndex(p1); renderMesh.addIndex(p3); renderMesh.addIndex(p2);
                renderMesh.addIndex(p2); renderMesh.addIndex(p3); renderMesh.addIndex(p4);
            }
        }
    }

    void drawDebug(float w, float h, int mode) {
        if(mode == EDIT_NONE) return;
        ofPushMatrix();
        ofScale(w, h, 1);
        auto& verts = (mode == EDIT_TEXTURE) ? sourceMesh.getVertices() : renderMesh.getVertices();
        
        ofSetLineWidth(2);
        ofSetColor(255, 0, 0); 
        for(int y=0; y<=rows; y++) {
            ofPolyline line;
            for(int x=0; x<=cols; x++) line.addVertex(verts[x + y*(cols+1)]);
            line.draw();
        }
        for(int x=0; x<=cols; x++) {
            ofPolyline line;
            for(int y=0; y<=rows; y++) line.addVertex(verts[x + y*(cols+1)]);
            line.draw();
        }
        for(size_t i=0; i<verts.size(); i++) {
            if((int)i == selectedPoint) { ofSetColor(255, 255, 0); ofDrawCircle(verts[i], 0.015); } 
            else { ofSetColor(0, 255, 255); ofDrawCircle(verts[i], 0.01); }
        }
        ofPopMatrix();
    }

    void draw(ofTexture& tex, float w, float h) {
        renderMesh.clearTexCoords();
        for(auto& v : sourceMesh.getVertices()) {
            renderMesh.addTexCoord(ofVec2f(v.x * tex.getWidth(), v.y * tex.getHeight()));
        }
        ofPushMatrix();
        ofScale(w, h, 1);
        tex.bind();
        renderMesh.draw();
        tex.unbind();
        ofPopMatrix();
    }
    
    int getHit(float x, float y, float w, float h, int mode) {
        if(mode == EDIT_NONE) return -1;
        auto& verts = (mode == EDIT_TEXTURE) ? sourceMesh.getVertices() : renderMesh.getVertices();
        float minD = 20;
        int hit = -1;
        for(size_t i=0; i<verts.size(); i++) {
            float d = ofDist(x, y, verts[i].x*w, verts[i].y*h);
            if(d < minD) { minD = d; hit = (int)i; }
        }
        return hit;
    }
    
    void updatePoint(int idx, float x, float y, int mode) {
        if(mode == EDIT_NONE) return;
        auto& verts = (mode == EDIT_TEXTURE) ? sourceMesh.getVertices() : renderMesh.getVertices();
        if(idx >= 0 && idx < (int)verts.size()) verts[idx] = glm::vec3(x, y, 0);
    }
    
    ofJson toJson() {
        ofJson j;
        j["rows"] = rows; j["cols"] = cols; j["id"] = id; j["owner"] = ownerId;
        for(auto& v : renderMesh.getVertices()) j["geo"].push_back({{"x",v.x}, {"y",v.y}});
        for(auto& v : sourceMesh.getVertices()) j["tex"].push_back({{"x",v.x}, {"y",v.y}});
        return j;
    }
    
    void fromJson(ofJson j) {
        ownerId = j.value("owner", "unknown");
        setup(j.value("rows", 3), j.value("cols", 3));
        id = j.value("id", "0000");
        auto& geo = renderMesh.getVertices();
        auto& tex = sourceMesh.getVertices();
        for(size_t i=0; i<j["geo"].size() && i<geo.size(); i++) 
            geo[i] = glm::vec3(j["geo"][i]["x"], j["geo"][i]["y"], 0);
        for(size_t i=0; i<j["tex"].size() && i<tex.size(); i++) 
            tex[i] = glm::vec3(j["tex"][i]["x"], j["tex"][i]["y"], 0);
    }
};