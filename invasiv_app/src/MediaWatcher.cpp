#include "MediaWatcher.h"
#include "TinyMD5.h"

MediaWatcher::MediaWatcher() {
}

MediaWatcher::~MediaWatcher() {
    isRunning = false;
    if (watcherThread.joinable()) {
        watcherThread.join();
    }
}

void MediaWatcher::setup(const std::string& mediaFolder) {
    isRunning = false;
    if (watcherThread.joinable()) {
        watcherThread.join();
    }

    {
        std::lock_guard<std::mutex> lockData(dataMutex);
        watchedFiles.clear();
    }
    {
        std::lock_guard<std::mutex> lockQueue(queueMutex);
        eventQueue.clear();
    }

    mediaRoot = ofFilePath::getAbsolutePath(ofToDataPath(mediaFolder, true));

    isRunning = true;
    watcherThread = std::thread(&MediaWatcher::threadLoop, this);
}

void MediaWatcher::setCheckInterval(float seconds) {
    intervalMs = static_cast<int>(std::max(0.01f, seconds) * 1000.0f);
}

void MediaWatcher::setSettlingTime(float seconds) {
    settlingMs = static_cast<int>(std::max(0.0f, seconds) * 1000.0f);
}

std::vector<std::string> MediaWatcher::getAllItems() {
    std::lock_guard<std::mutex> lock(dataMutex);
    std::vector<std::string> items;
    for (const auto& pair : watchedFiles) {
        items.push_back(pair.first);
    }
    std::sort(items.begin(), items.end());
    return items;
}

void MediaWatcher::update() {
    std::vector<std::string> changes;
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        if (eventQueue.empty()) return;
        changes = std::move(eventQueue);
        eventQueue.clear();
    }
    if (!changes.empty()) ofNotifyEvent(filesChanged, changes, this);
}

void MediaWatcher::threadLoop() {
    while (isRunning) {
        auto start = std::chrono::steady_clock::now();
        scan();
        auto duration = std::chrono::steady_clock::now() - start;
        auto sleepTime = std::chrono::milliseconds(intervalMs) - duration;
        if (sleepTime.count() > 0) std::this_thread::sleep_for(sleepTime);
        else std::this_thread::sleep_for(std::chrono::milliseconds(10)); 
    }
}

void MediaWatcher::scan() {
    std::vector<std::string> localChanges;
    auto now = std::chrono::steady_clock::now();
    int settleTime = settlingMs.load();
    std::unordered_set<std::string> seen;

    {
        std::lock_guard<std::mutex> lock(dataMutex);

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
                            PathInfo info;
                            info.last_md5 = "";
                            info.lastTime = fs::file_time_type::min();
                            info.candidateTime = diskTime;
                            info.stabilizationStart = now;
                            info.isSettling = true;
                            watchedFiles[relStr] = info;
                            continue;
                        }

                        PathInfo& info = it->second;
                        if (diskTime != info.lastTime) {
                            if (!info.isSettling) {
                                info.isSettling = true;
                                info.candidateTime = diskTime;
                                info.stabilizationStart = now;
                            } else {
                                if (diskTime != info.candidateTime) {
                                    info.candidateTime = diskTime;
                                    info.stabilizationStart = now;
                                } else {
                                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - info.stabilizationStart).count();
                                    if (elapsed > settleTime) {
                                        std::string new_md5 = TinyMD5::getFileMD5(absPath.string());
                                        if (new_md5 != "00000000000000000000000000000000") {
                                            if (new_md5 != info.last_md5) {
                                                localChanges.push_back(relStr);
                                                info.last_md5 = new_md5;
                                            }
                                            info.lastTime = diskTime;
                                            info.isSettling = false;
                                        }
                                    }
                                }
                            }
                        } else {
                            info.isSettling = false;
                        }
                    }
                }
            } catch (...) {}
        }

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
