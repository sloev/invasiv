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
    void draw(Identity &identity, Network &net, WarpController &warper, MediaWatcher &watcher, char* pathInputBuf, string &projectPath);

private:
    ofxImGui::Gui gui;
    void drawPerformUi(Identity &identity, Network &net, WarpController &warper, MediaWatcher &watcher);
    void drawEditingUI(Identity &identity, Network &net, WarpController &warper, MediaWatcher &watcher, char* pathInputBuf, string &projectPath);
};
