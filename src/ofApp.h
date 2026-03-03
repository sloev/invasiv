#pragma once
#include "ofMain.h"
#include "Identity.h"
#include "Network.h"
#include "WarpController.h"
#include "MediaWatcher.h"
#include "GuiManager.h"
#include "StateManager.h"
#include "Metronome.h"
#include "AppComponents.h"

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

    void saveSettings(string path);
    string loadSettings();

    void syncFullState(); 
    
    bool bHeadless = false;

    Identity identity;
    Network net;
    WarpController warper;
    MediaWatcher watcher;
    StateManager stateMgr;
    Metronome metro;
    GuiManager gui;
    
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
