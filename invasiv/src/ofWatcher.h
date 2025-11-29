#pragma once

#include "ofMain.h"
#include <filesystem>
#include <chrono>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;


class ofWatcher {
public:
    void addPath(const std::string& path);
    void removePath(const std::string& path);
    void setCheckInterval(float seconds = 1.0f);
    void update();  // call from ofApp::update()

    // One global event – vector of changed file paths
    ofEvent<std::vector<std::string>> filesChanged;

private:
    std::unordered_set<std::string> watchedRoots;
    std::unordered_map<std::string, std::uint64_t> watchedFiles;  // store milliseconds
    float checkInterval = 1.0f;
    float lastCheckTime = 0;

    void addDirectoryRecursive(const std::string& dirPath);

    static std::uint64_t fileTimeToMs(const fs::file_time_type& ft) {
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        return std::chrono::duration_cast<std::chrono::milliseconds>(sctp.time_since_epoch()).count();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
inline void ofWatcher::addPath(const std::string& path)
{
    std::string abs = ofFilePath::getAbsolutePath(ofToDataPath(path, true));
    if (!watchedRoots.insert(abs).second) return;

    ofDirectory dir(abs);
    if (dir.exists() && dir.isDirectory()) {
        addDirectoryRecursive(abs);
    } else if (ofFile(abs).exists()) {
        watchedFiles[abs] = fileTimeToMs(fs::last_write_time(abs));
    }
}

inline void ofWatcher::removePath(const std::string& path)
{
    std::string abs = ofFilePath::getAbsolutePath(ofToDataPath(path, true));
    watchedRoots.erase(abs);

    auto it = watchedFiles.begin();
    while (it != watchedFiles.end()) {
        if (it->first.find(abs) == 0) it = watchedFiles.erase(it);
        else ++it;
    }
}

inline void ofWatcher::setCheckInterval(float seconds)
{
    checkInterval = std::max(0.1f, seconds);
}

inline void ofWatcher::update()
{
    if (ofGetElapsedTimef() - lastCheckTime < checkInterval) return;
    lastCheckTime = ofGetElapsedTimef();

    std::unordered_map<std::string, std::vector<std::string>> changes;

    // Check existing files
    auto it = watchedFiles.begin();
    while (it != watchedFiles.end()) {
        const std::string& p = it->first;

        if (!fs::exists(p)) {
            changes[ofFilePath::getEnclosingDirectory(p)].push_back(p);
            it = watchedFiles.erase(it);
            continue;
        }

        auto newMs = fileTimeToMs(fs::last_write_time(p));
        if (newMs != it->second) {
            it->second = newMs;
            changes[ofFilePath::getEnclosingDirectory(p)].push_back(p);
        }
        ++it;
    }

    // Discover new files
    for (const auto& root : watchedRoots) {
        try {
            for (const auto& entry : fs::recursive_directory_iterator(root,
                     fs::directory_options::skip_permission_denied)) {
                if (entry.is_regular_file()) {
                    std::string abs = entry.path().string();
                    if (watchedFiles.count(abs) == 0) {
                        watchedFiles[abs] = fileTimeToMs(entry.last_write_time());
                        changes[root].push_back(abs);
                    }
                }
            }
        } catch (...) { }
    }

    // Notify – collect all changed paths into one vector
    std::vector<std::string> allChanged;
    for (auto& pair : changes) {
        allChanged.insert(allChanged.end(), pair.second.begin(), pair.second.end());
    }
    if (!allChanged.empty()) {
        ofNotifyEvent(filesChanged, allChanged, this);
    }
}

inline void ofWatcher::addDirectoryRecursive(const std::string& dirPath)
{
    try {
        for (const auto& entry : fs::recursive_directory_iterator(dirPath,
                 fs::directory_options::skip_permission_denied)) {
            if (entry.is_regular_file()) {
                std::string p = entry.path().string();
                watchedFiles[p] = fileTimeToMs(entry.last_write_time());
            }
        }
    } catch (...) { }
}

