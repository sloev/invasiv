// ofWatcher.h
// Modern OF-style recursive file watcher with one-liner callback registration
// Fully compatible with openFrameworks 0.12.1+
// Single header, MIT license

#pragma once

#include "ofMain.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

class ofWatcher {
public:
    // Type of the event: passes const ref to vector of changed absolute paths
    using ChangedEvent = ofEvent<const std::vector<std::string>&>;

    ofWatcher() : checkInterval(1.0f), lastCheckTime(0.0f) {}
    ~ofWatcher() = default;

    // ONE-LINE USAGE: Add path + register callback in one go
    template<typename T>
    void addPath(const std::string& path, T* listener, void(T::*method)(const std::vector<std::string>&)) {
        std::string absPath = ofFile(ofToDataPath(path, true)).getAbsolutePath();

        if (!ofFile(absPath).exists()) {
            ofLogWarning("ofWatcher") << "Path does not exist: " << path;
            return;
        }

        // Ensure event exists for this root
        auto& event = rootEvents[absPath];

        // Register the listener method
        ofAddListener(event, listener, method);

        // First time we see this root? Start watching it
        if (watchedRoots.insert(absPath).second) {
            if (ofDirectory(absPath).exists()) {
                addDirectoryRecursive(absPath);
            } else {
                watchedFiles[absPath] = ofFile(absPath).getPocoFile().getLastModified().rawTime();
            }
        }
    }

    // Optional: Add path without any listener (just for tracking)
    void addPath(const std::string& path) {
        std::string absPath = ofFile(ofToDataPath(path, true)).getAbsolutePath();
        if (!ofFile(absPath).exists()) {
            ofLogWarning("ofWatcher") << "Path does not exist: " << path;
            return;
        }
        if (watchedRoots.insert(absPath).second) {
            if (ofDirectory(absPath).exists()) {
                addDirectoryRecursive(absPath);
            } else {
                watchedFiles[absPath] = ofFile(absPath).getPocoFile().getLastModified().rawTime();
            }
        }
    }

    // Remove path and all its listeners automatically
    void removePath(const std::string& path) {
        std::string absPath = ofFile(ofToDataPath(path, true)).get   
        rootEvents.erase(absPath);
        watchedRoots.erase(absPath);

        auto it = watchedFiles.begin();
        while (it != watchedFiles.end()) {
            if (it->first.find(absPath) == 0) {
                it = watchedFiles.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Set scan interval (seconds)
    void setCheckInterval(float seconds) {
        checkInterval = std::max(0.1f, seconds);
    }

    // Call every frame
    void update() {
        float now = ofGetElapsedTimef();
        if (now - lastCheckTime < checkInterval) return;
        lastCheckTime = now;

        std::unordered_map<std::string, std::vector<std::string>> changesPerRoot;

        // 1. Check existing files
        auto it = watchedFiles.begin();
        while (it != watchedFiles.end()) {
            const std::string& path = it->first;
            ofFile file(path);

            if (!file.exists()) {
                for (const auto& root : watchedRoots) {
                    if (path.find(root) == 0) {
                        changesPerRoot[root].push_back(path);
                        break;
                    }
                }
                it = watchedFiles.erase(it);
            } else {
                auto newTime = file.getPocoFile().getLastModified().rawTime();
                if (newTime != it->second) {
                    for (const auto& root : watchedRoots) {
                        if (path.find(root) == 0) {
                            changesPerRoot[root].push_back(path);
                            break;
                        }
                    }
                    it->second = newTime;
                }
                ++it;
            }
        }

        // 2. Discover new files
        for (const auto& root : watchedRoots) {
            ofFile rootFile(root);
            if (!rootFile.isDirectory()) continue;

            ofDirectory dir(root);
            dir.allowExt("");
            auto files = dir.getFiles(true);

            for (const auto& f : files) {
                if (f.isDirectory()) continue;
                std::string abs = f.getAbsolutePath();
                if (watchedFiles.find(abs) == watchedFiles.end()) {
                    changesPerRoot[root].push_back(abs);
                    watchedFiles[abs] = f.getPocoFile().getLastModified().rawTime();
                }
            }
        }

        // 3. Notify all listeners per root
        for (const auto& pair : changesPerRoot) {
            const std::string& root = pair.first;
            const auto& changed = pair.second;

            auto eventIt = rootEvents.find(root);
            if (eventIt != rootEvents.end()) {
                ofNotifyEvent(eventIt->second, changed, this);
            }
        }
    }

private:
    void addDirectoryRecursive(const std::string& dirPath) {
        ofDirectory dir(dirPath);
        dir.allowExt("");
        auto files = dir.getFiles(true);
        for (const auto& f : files) {
            if (!f.isDirectory()) {
                watchedFiles[f.getAbsolutePath()] = f.getPocoFile().getLastModified().rawTime();
            }
        }
    }

    std::unordered_map<std::string, Poco::Timestamp::TimeVal> watchedFiles;
    std::unordered_set<std::string> watchedRoots;
    std::unordered_map<std::string, ChangedEvent> rootEvents;

    float checkInterval;
    float lastCheckTime;
};