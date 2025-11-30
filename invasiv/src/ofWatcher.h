#pragma once

#include "ofMain.h"
#include <filesystem>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <mutex>
#include <atomic>

namespace fs = std::filesystem;

class ofWatcher {
public:
    ofWatcher();
    ~ofWatcher();

    void addPath(const std::string& path);
    void removePath(const std::string& path);
    
    // Time between scans. 0.1s is responsive without hogging CPU.
    void setCheckInterval(float seconds);

    // Minimum time a file must remain unchanged before firing event.
    // Fixes "Upload Fail" by ensuring file is fully written/closed.
    void setSettlingTime(float seconds);
    
    void update(); // Call in ofApp::update()

    ofEvent<std::vector<std::string>> filesChanged;

private:
    struct PathInfo {
        fs::file_time_type lastTime; // The last confirmed synced time
        bool exists;
        
        // Debouncing logic
        fs::file_time_type candidateTime; // The new time we just spotted
        std::chrono::steady_clock::time_point stabilizationStart; // When we spotted it
        bool isSettling = false; // Are we currently waiting for it to stabilize?
    };

    std::thread watcherThread;
    std::atomic<bool> isRunning{false};
    std::mutex dataMutex;
    std::mutex queueMutex;
    
    std::atomic<int> intervalMs{200};
    std::atomic<int> settlingMs{250}; // Default 250ms wait to ensure file is unlocked

    std::unordered_set<std::string> watchedRoots;
    std::unordered_map<std::string, PathInfo> watchedFiles;
    std::vector<std::string> eventQueue;

    void threadLoop();
    void scan();
    void addFileInternal(const std::string& absPath);
};

// ─────────────────────────────────────────────────────────────────────────────
// Implementation
// ─────────────────────────────────────────────────────────────────────────────

inline ofWatcher::ofWatcher() {
    isRunning = true;
    watcherThread = std::thread(&ofWatcher::threadLoop, this);
}

inline ofWatcher::~ofWatcher() {
    isRunning = false;
    if (watcherThread.joinable()) {
        watcherThread.join();
    }
}

inline void ofWatcher::setCheckInterval(float seconds) {
    intervalMs = static_cast<int>(std::max(0.01f, seconds) * 1000.0f);
}

inline void ofWatcher::setSettlingTime(float seconds) {
    settlingMs = static_cast<int>(std::max(0.0f, seconds) * 1000.0f);
}

inline void ofWatcher::addPath(const std::string& path) {
    std::string abs = ofFilePath::getAbsolutePath(ofToDataPath(path, true));
    std::lock_guard<std::mutex> lock(dataMutex);
    
    if (fs::is_directory(abs)) {
        watchedRoots.insert(abs);
        try {
            for (const auto& entry : fs::recursive_directory_iterator(abs, fs::directory_options::skip_permission_denied)) {
                if (entry.is_regular_file()) {
                    addFileInternal(entry.path().string());
                }
            }
        } catch (...) {}
    } else if (fs::exists(abs)) {
        addFileInternal(abs);
    }
}

inline void ofWatcher::addFileInternal(const std::string& absPath) {
    std::error_code ec;
    auto ftime = fs::last_write_time(absPath, ec);
    if (!ec) {
        // Initialize with isSettling = false so we don't trigger immediately on startup
        watchedFiles[absPath] = { ftime, true, ftime, {}, false };
    }
}

inline void ofWatcher::removePath(const std::string& path) {
    std::string abs = ofFilePath::getAbsolutePath(ofToDataPath(path, true));
    std::lock_guard<std::mutex> lock(dataMutex);
    watchedRoots.erase(abs);
    auto it = watchedFiles.begin();
    while (it != watchedFiles.end()) {
        if (it->first.find(abs) == 0) it = watchedFiles.erase(it);
        else ++it;
    }
}

inline void ofWatcher::update() {
    std::vector<std::string> changes;
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        if (eventQueue.empty()) return;
        changes = std::move(eventQueue);
        eventQueue.clear();
    }
    if (!changes.empty()) ofNotifyEvent(filesChanged, changes, this);
}

inline void ofWatcher::threadLoop() {
    while (isRunning) {
        auto start = std::chrono::steady_clock::now();
        scan();
        auto duration = std::chrono::steady_clock::now() - start;
        auto sleepTime = std::chrono::milliseconds(intervalMs) - duration;
        if (sleepTime.count() > 0) std::this_thread::sleep_for(sleepTime);
        else std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Prevent CPU burn
    }
}

inline void ofWatcher::scan() {
    std::vector<std::string> localChanges;
    auto now = std::chrono::steady_clock::now();
    int settleTime = settlingMs.load();

    {
        std::lock_guard<std::mutex> lock(dataMutex);

        // 1. Check existing files
        auto it = watchedFiles.begin();
        while (it != watchedFiles.end()) {
            const std::string& path = it->first;
            PathInfo& info = it->second;
            std::error_code ec;
            
            // Check existence
            bool currentExists = fs::exists(path, ec);

            if (!currentExists) {
                if (info.exists) {
                    // DELETED: No settling needed for deletions
                    localChanges.push_back(path);
                    it = watchedFiles.erase(it);
                    continue;
                }
            } else {
                auto diskTime = fs::last_write_time(path, ec);
                
                if (!ec) {
                    // Logic: Has the file changed from our 'confirmed' time?
                    if (diskTime != info.lastTime) {
                        
                        if (!info.isSettling) {
                            // First time noticing the change. Start settling.
                            info.isSettling = true;
                            info.candidateTime = diskTime;
                            info.stabilizationStart = now;
                        } 
                        else {
                            // We are already watching it settle.
                            if (diskTime != info.candidateTime) {
                                // It changed AGAIN! Reset timer.
                                info.candidateTime = diskTime;
                                info.stabilizationStart = now;
                            } 
                            else {
                                // Timestamp is stable. Has enough time passed?
                                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - info.stabilizationStart).count();
                                if (elapsed > settleTime) {
                                    // STABLE! Commit and Fire.
                                    info.lastTime = diskTime;
                                    info.isSettling = false;
                                    localChanges.push_back(path);
                                }
                            }
                        }
                    } else {
                        // timestamps match, ensure settling is off
                        info.isSettling = false;
                    }
                }
            }
            ++it;
        }

        // 2. Discover new files (Roots)
        for (const auto& root : watchedRoots) {
            try {
                if (!fs::exists(root)) continue;
                for (const auto& entry : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied)) {
                    if (entry.is_regular_file()) {
                        std::string p = entry.path().string();
                        if (watchedFiles.find(p) == watchedFiles.end()) {
                            // Found new file. Add it, but don't fire yet.
                            // We treat "new" as "modified" to let it settle (in case it's still copying).
                            addFileInternal(p);
                            
                            // Immediately mark as settling so we wait for the copy to finish
                            watchedFiles[p].lastTime = fs::file_time_type::min(); // Force mismatch
                            watchedFiles[p].isSettling = true;
                            watchedFiles[p].candidateTime = entry.last_write_time();
                            watchedFiles[p].stabilizationStart = now;
                        }
                    }
                }
            } catch (...) {}
        }
    }

    if (!localChanges.empty()) {
        std::lock_guard<std::mutex> lock(queueMutex);
        eventQueue.insert(eventQueue.end(), localChanges.begin(), localChanges.end());
    }
}