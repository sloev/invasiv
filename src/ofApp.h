#pragma once
#include "ofMain.h"
#include "Core.h"
#include "GuiManager.h"
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
    void onFilesChanged(std::vector<std::string>& files);
    void exit();
    
    void audioIn(ofSoundBuffer & input);

    bool bHeadless = false;
    Core core;
    GuiManager gui;
    
    ofSoundStream soundStream;
    
    char pathInputBuf[256];
    
    bool bShowHelp = true;
    float helpTimer = 10.0f;
};
