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

    std::string mediaRoot; 
    std::thread watcherThread;
    std::atomic<bool> isRunning{false};
    std::mutex dataMutex;
    std::mutex queueMutex;

    std::atomic<int> intervalMs{200}; 
    std::atomic<int> settlingMs{250}; 

    std::unordered_map<std::string, PathInfo> watchedFiles; 
    std::vector<std::string> eventQueue;

    void threadLoop();
    void scan();
};
