#pragma once
#include "ofMain.h"

class Identity {
public:
    string myId;
    bool fullscreen = false;
    string configPath;

    void setup(string _configPath);
    void toggleFullscreen();
    void save();

private:
    string generateID();
};
