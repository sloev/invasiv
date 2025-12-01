#pragma once
#include "ofMain.h"
#include "ofxImGui.h"
#include "Identity.h"
#include "Network.h"
#include "WarpController.h"
#include "Content.h"

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

    Identity identity;
    Network net;
    Content content;
    WarpController warper;
    
    ofxImGui::Gui gui;
    void drawUI();

    string projectPath;
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