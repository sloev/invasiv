#pragma once
#include "ofMain.h"
#include "Identity.h"
#include "Network.h"
#include "WarpController.h"
#include "MediaWatcher.h"
#include "StateManager.h"
#include "Metronome.h"
#include "BeatTracker.h"

class Core {
public:
    void setup(bool headless);
    void update();
    void exit();

    void reloadProject(string path);
    void onFilesChanged(std::vector<std::string>& files);
    void syncFullState();
    
    void saveSettings(string path);
    string loadSettings();

    // Core state and systems
    bool bHeadless = false;
    Identity identity;
    Network net;
    WarpController warper;
    MediaWatcher watcher;
    StateManager stateMgr;
    Metronome metro;
    BeatTracker tracker;
    
    string projectPath;
    string mediaDir;

    struct {
        bool active = false;
        string name;
        uint32_t total;
        uint32_t current;
        ofBuffer buf;
    } incoming;

private:
    char packetBuffer[65535];
    void handlePackets();
};
