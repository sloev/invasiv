#pragma once
#include "ofMain.h"
#include "ofxImGui.h"
#include "Identity.h"
#include "Network.h"
#include "WarpController.h"
#include "MediaWatcher.h"

class GuiManager {
public:
    void setup();
    void draw(Identity &identity, Network &net, WarpController &warper, MediaWatcher &watcher, StateManager &stateMgr, char* pathInputBuf, string &projectPath);

private:
    ofxImGui::Gui gui;
    char idInputBuf[64] = "";
    char surfaceIdInputBuf[64] = "";
    string lastIdOwner = "";
    string lastSurfaceId = "";

    void drawPerformUi(Identity &identity, Network &net, WarpController &warper, MediaWatcher &watcher, StateManager &stateMgr);
    void drawEditingUI(Identity &identity, Network &net, WarpController &warper, MediaWatcher &watcher, StateManager &stateMgr, char* pathInputBuf, string &projectPath);
};
