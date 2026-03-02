#pragma once
#include "ofMain.h"
#include "ofxImGui.h"
#include "AppComponents.h"

class GuiManager {
public:
    void setup();
    void draw(AppComponents &c);

private:
    ofxImGui::Gui gui;
    char idInputBuf[64] = "";
    char surfaceIdInputBuf[64] = "";
    string lastIdOwner = "";
    string lastSurfaceId = "";

    void drawPerformUi(AppComponents &c);
    void drawEditingUI(AppComponents &c);
};
