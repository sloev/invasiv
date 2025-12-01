#pragma once
#include "ofMain.h"

class Identity {
public:
    string myId;
    string configPath;

    void setup(string _configPath) {
        configPath = _configPath;
        ofFile file(configPath);
        ofJson config;

        if(file.exists()) {
            file >> config;
            if(config.contains("identity") && config["identity"].contains("id")) {
                myId = config["identity"]["id"].get<string>();
            }
        }

        if(myId.length() != 8) {
            myId = generateID();
            save();
            ofLogNotice("Identity") << "Generated New ID: " << myId;
        } else {
            ofLogNotice("Identity") << "Loaded ID: " << myId;
        }
    }
    
    void save() {
        ofJson config;
        config["identity"]["id"] = myId;
        ofSaveJson(configPath, config);
    }

private:
    string generateID() {
        static const char alphanum[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        string s = "";
        for (int i = 0; i < 8; ++i) s += alphanum[(int)ofRandom(0, 61)];
        return s;
    }
};