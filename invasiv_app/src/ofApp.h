#pragma once
#include "ofMain.h"
#include "ofxImGui.h"
#include "Identity.h"
#include "Network.h"
#include "WarpController.h"
#include "MediaWatcher.h"

class ofApp : public ofBaseApp{
public:
    void setup();
    void update();
    void draw();
    
    void mousePressed(int x, int y, int button);
    void mouseDragged(int x, int y, int button);
    void mouseReleased(int x, int y, int button);
    void keyPressed(int key);
    void reloadProject(string path);
    void onFilesChanged(std::vector<std::string>& files);
    void exit();

    // -- NEW: Helper to sync everything to peers --
    void syncFullState(); 
    
    Identity identity;
    Network net;
    WarpController warper;
    MediaWatcher watcher;
    
    ofxImGui::Gui gui;
    void drawUI();

    string projectPath;
    string mediaDir;
    char pathInputBuf[256];

    struct {
        bool active = false;
        string name;
        uint32_t total;
        uint32_t current;
        ofBuffer buf;
    } incoming;
    
    char packetBuffer[65535];
};