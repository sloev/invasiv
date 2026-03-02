#include "StateManager.h"
#include "WarpController.h"
#include "Network.h"

void StateManager::setup(string path) {
    configPath = path;
    triggersPath = ofFilePath::removeExt(path) + "_triggers.json";
    load();
}

void StateManager::addTrigger(int key, int stateIndex) {
    triggers.push_back({key, stateIndex});
    save();
}

void StateManager::removeTrigger(int index) {
    if(index >= 0 && index < (int)triggers.size()) {
        triggers.erase(triggers.begin() + index);
        save();
    }
}

void StateManager::processKey(int key, WarpController &warper, Network &net) {
    for(auto &t : triggers) {
        if(t.key == key) {
            applyState(t.stateIndex, warper, net);
            break;
        }
    }
}

void StateManager::saveState(string name, WarpController &warper) {
    State s;
    s.name = name;
    
    ofJson root;
    map<string, ofJson> groups;
    for (auto &surf : warper.allSurfaces)
        groups[surf->ownerId].push_back(surf->toJson());
    for (auto &kv : groups)
        root["peers"][kv.first] = kv.second;
        
    s.data = root;
    
    bool found = false;
    for(auto &st : states) {
        if(st.name == name) {
            st.data = s.data;
            found = true;
            break;
        }
    }
    if(!found) states.push_back(s);
    save();
}

void StateManager::applyState(int index, WarpController &warper, Network &net) {
    if(index >= 0 && index < (int)states.size()) {
        currentStateIndex = index;
        string jStr = states[index].data.dump();
        warper.loadJson(jStr);
        net.sendStructure(jStr);
    }
}

void StateManager::removeState(int index) {
    if(index >= 0 && index < (int)states.size()) {
        states.erase(states.begin() + index);
        save();
    }
}

void StateManager::save() {
    ofJson j = ofJson::array();
    for(auto &s : states) {
        j.push_back({{"name", s.name}, {"data", s.data}});
    }
    ofSaveJson(configPath, j);
    
    ofJson jt = ofJson::array();
    for(auto &t : triggers) {
        jt.push_back({{"key", t.key}, {"stateIndex", t.stateIndex}});
    }
    ofSaveJson(triggersPath, jt);
}

void StateManager::load() {
    ofFile file(configPath);
    if(file.exists()) {
        ofJson j;
        file >> j;
        states.clear();
        for(auto &item : j) {
            State s;
            s.name = item.value("name", "Untitled");
            s.data = item["data"];
            states.push_back(s);
        }
    }
    
    ofFile tfile(triggersPath);
    if(tfile.exists()) {
        ofJson j;
        tfile >> j;
        triggers.clear();
        for(auto &item : j) {
            Trigger t;
            t.key = item.value("key", 0);
            t.stateIndex = item.value("stateIndex", 0);
            triggers.push_back(t);
        }
    }
}
