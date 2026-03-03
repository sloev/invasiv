#pragma once
#include "ofMain.h"

class Identity;
class Network;
class WarpController;
class MediaWatcher;
class StateManager;
class Metronome;
class Core;

struct AppComponents {
    Identity &identity;
    Network &net;
    WarpController &warper;
    MediaWatcher &watcher;
    StateManager &stateMgr;
    Metronome &metro;
    char* pathInputBuf;
    string &projectPath;
    Core &core;
};
