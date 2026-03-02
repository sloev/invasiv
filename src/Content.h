#pragma once
#include "ofMain.h"
#include "ofxMPVPlayer.h"
#include "Metronome.h"
#include <map>
#include <memory>
#include <vector>
#include <thread>
#include <atomic>

#define DEFAULT_CONTENT "default"

class TestTexture
{
public:
    TestTexture(const TestTexture &) = delete;
    void operator=(const TestTexture &) = delete;
    static TestTexture &getInstance();
    ofTexture &getTexture();
    ofTexture tex;

private:
    TestTexture() {}
};

class Content
{
public:
    virtual ~Content() {}
    virtual void setup(string id = "") {}
    virtual void start() {}
    virtual void stop() {}
    virtual void update() {}
    virtual ofTexture &getTexture();
    virtual void setMetronome(Metronome* m) {}
};

class VideoContent : public Content
{
private:
    enum State {
        DORMANT,
        LOADING,
        READY,
        ERROR
    };

    std::shared_ptr<ofxMPVPlayer> video;
    string filePath;
    Metronome* metro = nullptr;
    std::atomic<State> state{DORMANT};
    std::thread loaderThread;
    
    uint64_t lastRequestFrame = 0;
    bool bWantsToPlay = false;

    void loadAsync();

public:
    VideoContent() = default;
    ~VideoContent();

    void setup(string filename) override;
    void setMetronome(Metronome* m) override { metro = m; if(video) video->metro = m; }
    void start() override;
    void stop() override;
    void update() override;
    ofTexture &getTexture() override;
};

class ContentManager
{
private:
    std::map<std::string, std::shared_ptr<Content>> contents;
    std::map<std::string, uint64_t> lastUsedFrame;
    Metronome* metro = nullptr;

public:
    void setup();
    void setMetronome(Metronome* m) { metro = m; }
    vector<string> getContentNames();
    bool registerContent(std::string id, std::shared_ptr<Content> c);
    void refreshMedia(string mediaPath);
    ofTexture &getTextureById(std::string id);
    void update();
};
