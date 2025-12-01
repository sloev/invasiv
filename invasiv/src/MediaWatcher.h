#pragma once

#include "ofMain.h"
#include <filesystem>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include "TinyMD5.h"

namespace fs = std::filesystem;

class MediaWatcher {
public:
    MediaWatcher();
    ~MediaWatcher();

    void setup(const std::string& mediaFolder);

    void setCheckInterval(float seconds);
    void setSettlingTime(float seconds);

    void update(); // Call in ofApp::update()

    std::vector<std::string> getAllItems();

    ofEvent<std::vector<std::string>> filesChanged;

private:
    struct PathInfo {
        std::string last_md5;
        fs::file_time_type lastTime;
        fs::file_time_type candidateTime;
        std::chrono::steady_clock::time_point stabilizationStart;
        bool isSettling = false;
    };

    std::string mediaRoot; // Absolute path to media folder
    std::thread watcherThread;
    std::atomic<bool> isRunning{false};
    std::mutex dataMutex;
    std::mutex queueMutex;

    std::atomic<int> intervalMs{200}; // Default 200ms
    std::atomic<int> settlingMs{250}; // Default 250ms

    std::unordered_map<std::string, PathInfo> watchedFiles; // key: relative path
    std::vector<std::string> eventQueue;

    void threadLoop();
    void scan();
};

// ─────────────────────────────────────────────────────────────────────────────
// Implementation
// ─────────────────────────────────────────────────────────────────────────────

inline MediaWatcher::MediaWatcher() {
    // Thread not started yet; call setup() to initialize
}

inline MediaWatcher::~MediaWatcher() {
    isRunning = false;
    if (watcherThread.joinable()) {
        watcherThread.join();
    }
}

inline void MediaWatcher::setup(const std::string& mediaFolder) {
    // Stop existing thread if running
    isRunning = false;
    if (watcherThread.joinable()) {
        watcherThread.join();
    }

    // Clear data
    {
        std::lock_guard<std::mutex> lockData(dataMutex);
        watchedFiles.clear();
    }
    {
        std::lock_guard<std::mutex> lockQueue(queueMutex);
        eventQueue.clear();
    }

    // Set new root
    mediaRoot = ofFilePath::getAbsolutePath(ofToDataPath(mediaFolder, true));

    // Start thread
    isRunning = true;
    watcherThread = std::thread(&MediaWatcher::threadLoop, this);
}

inline void MediaWatcher::setCheckInterval(float seconds) {
    intervalMs = static_cast<int>(std::max(0.01f, seconds) * 1000.0f);
}

inline void MediaWatcher::setSettlingTime(float seconds) {
    settlingMs = static_cast<int>(std::max(0.0f, seconds) * 1000.0f);
}

inline std::vector<std::string> MediaWatcher::getAllItems() {
    std::lock_guard<std::mutex> lock(dataMutex);
    std::vector<std::string> items;
    for (const auto& pair : watchedFiles) {
        items.push_back(pair.first);
    }
    std::sort(items.begin(), items.end());
    return items;
}

inline void MediaWatcher::update() {
    std::vector<std::string> changes;
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        if (eventQueue.empty()) return;
        changes = std::move(eventQueue);
        eventQueue.clear();
    }
    if (!changes.empty()) ofNotifyEvent(filesChanged, changes, this);
}

inline void MediaWatcher::threadLoop() {
    while (isRunning) {
        auto start = std::chrono::steady_clock::now();
        scan();
        auto duration = std::chrono::steady_clock::now() - start;
        auto sleepTime = std::chrono::milliseconds(intervalMs) - duration;
        if (sleepTime.count() > 0) std::this_thread::sleep_for(sleepTime);
        else std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Prevent CPU burn
    }
}

inline void MediaWatcher::scan() {
    std::vector<std::string> localChanges;
    auto now = std::chrono::steady_clock::now();
    int settleTime = settlingMs.load();
    std::unordered_set<std::string> seen;

    {
        std::lock_guard<std::mutex> lock(dataMutex);

        // 1. Discover new files and check existing ones
        if (fs::exists(mediaRoot)) {
            try {
                for (const auto& entry : fs::recursive_directory_iterator(mediaRoot, fs::directory_options::skip_permission_denied)) {
                    if (entry.is_regular_file()) {
                        fs::path absPath = entry.path();
                        std::string relStr = fs::relative(absPath, mediaRoot).string();
                        if (relStr.size() >= 4 && relStr.substr(relStr.size() - 4) == ".tmp") continue;

                        seen.insert(relStr);

                        std::error_code ec;
                        auto diskTime = fs::last_write_time(absPath, ec);
                        if (ec) continue;

                        auto it = watchedFiles.find(relStr);
                        if (it == watchedFiles.end()) {
                            // New file: Add and start settling
                            PathInfo info;
                            info.last_md5 = "";
                            info.lastTime = fs::file_time_type::min();
                            info.candidateTime = diskTime;
                            info.stabilizationStart = now;
                            info.isSettling = true;
                            watchedFiles[relStr] = info;
                            continue;
                        }

                        // Existing file
                        PathInfo& info = it->second;
                        if (diskTime != info.lastTime) {
                            if (!info.isSettling) {
                                // Start settling
                                info.isSettling = true;
                                info.candidateTime = diskTime;
                                info.stabilizationStart = now;
                            } else {
                                // Already settling
                                if (diskTime != info.candidateTime) {
                                    // Changed again, reset
                                    info.candidateTime = diskTime;
                                    info.stabilizationStart = now;
                                } else {
                                    // Check if stable
                                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - info.stabilizationStart).count();
                                    if (elapsed > settleTime) {
                                        // Try to compute MD5
                                        std::string new_md5 = TinyMD5::getFileMD5(absPath.string());
                                        if (new_md5 != "00000000000000000000000000000000") { // Assume readable
                                            if (new_md5 != info.last_md5) {
                                                // Changed or new (if last_md5 empty)
                                                localChanges.push_back(relStr);
                                                info.last_md5 = new_md5;
                                            }
                                            info.lastTime = diskTime;
                                            info.isSettling = false;
                                        }
                                        // If not readable, continue settling
                                    }
                                }
                            }
                        } else {
                            // No change, reset settling
                            info.isSettling = false;
                        }
                    }
                }
            } catch (...) {}
        }

        // 2. Remove deleted files (no notification)
        auto it = watchedFiles.begin();
        while (it != watchedFiles.end()) {
            if (seen.find(it->first) == seen.end()) {
                it = watchedFiles.erase(it);
            } else {
                ++it;
            }
        }
    }

    if (!localChanges.empty()) {
        std::lock_guard<std::mutex> lock(queueMutex);
        eventQueue.insert(eventQueue.end(), localChanges.begin(), localChanges.end());
    }
}