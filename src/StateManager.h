#pragma once
#include "ofMain.h"

class WarpController;
class Network;

struct State {
    string name;
    ofJson data;
};

struct Trigger {
    int key;
    int stateIndex;
};

class StateManager {
public:
    vector<State> states;
    vector<Trigger> triggers;
    int currentStateIndex = -1;
    string configPath;
    string triggersPath;

    void setup(string path);
    void addTrigger(int key, int stateIndex);
    void removeTrigger(int index);
    void processKey(int key, WarpController &warper, Network &net);
    void saveState(string name, WarpController &warper);
    void applyState(int index, WarpController &warper, Network &net);
    void removeState(int index);
    void save();
    void load();
};
