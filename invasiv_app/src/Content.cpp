#include "Content.h"
#include <unordered_set>

TestTexture &TestTexture::getInstance()
{
    static TestTexture instance;
    return instance;
}

ofTexture &TestTexture::getTexture()
{
    if (!tex.isAllocated())
    {
        ofPixels pix;
        pix.allocate(256, 256, OF_PIXELS_RGB);
        for (int y = 0; y < 256; ++y)
        {
            for (int x = 0; x < 256; ++x)
            {
                ofColor c = ofColor::fromHsb((x / 255.0f) * 255, 200, 255);
                if ((x % 32) < 2 || (y % 32) < 2)
                    c = ofColor::red;
                pix.setColor(x, y, c);
            }
        }
        tex.loadData(pix);
    }
    return tex;
}

ofTexture &Content::getTexture()
{
    return TestTexture::getInstance().getTexture();
}

VideoContent::~VideoContent()
{
    if (loaderThread.joinable())
        loaderThread.join();
}

void VideoContent::setup(string filename)
{
    filePath = filename;
    state = DORMANT;
}

void VideoContent::loadAsync()
{
    if (state != DORMANT) return;
    state = LOADING;
    
    if (loaderThread.joinable()) loaderThread.join();
    
    loaderThread = std::thread([this]() {
        auto newPlayer = std::make_shared<ofxMPVPlayer>();
        if (newPlayer->load(filePath)) {
            newPlayer->setLoopState(OF_LOOP_NORMAL);
            this->video = newPlayer;
            this->state = READY;
        } else {
            this->state = ERROR;
        }
    });
}

void VideoContent::start()
{
    bWantsToPlay = true;
    if (state == DORMANT) {
        loadAsync();
    }
}

void VideoContent::stop()
{
    bWantsToPlay = false;
    if (video && state == READY) {
        video->setPaused(true);
        video->setPosition(0);
    }
}

void VideoContent::update()
{
    lastRequestFrame = ofGetFrameNum();
    
    if (state == READY && video) {
        if (bWantsToPlay) {
            if (video->isPaused()) video->setPaused(false);
            else if (!video->isPlaying()) video->play();
        }
        video->update();
    }
    
    // Auto-eviction if not used for 5 seconds
    if (state == READY && (ofGetFrameNum() - lastRequestFrame > 300)) {
        video.reset();
        state = DORMANT;
    }
}

ofTexture &VideoContent::getTexture()
{
    lastRequestFrame = ofGetFrameNum();
    if (state == READY && video && video->getWidth() > 0)
        return video->getTexture();
    else
        return TestTexture::getInstance().getTexture();
}

void ContentManager::setup()
{
    auto dtr = std::make_shared<Content>();
    dtr->setup();
    registerContent(DEFAULT_CONTENT, dtr);
}

vector<string> ContentManager::getContentNames()
{
    vector<string> names;
    for (auto const &kv : contents)
        names.push_back(kv.first);
    return names;
}

bool ContentManager::registerContent(std::string id, std::shared_ptr<Content> c)
{
    if (contents.count(id))
        return false;
    ofLogNotice("ContentManager") << "Registered content: " << id;
    contents[id] = c;
    return true;
}

void ContentManager::refreshMedia(string mediaPath)
{
    ofDirectory dir(mediaPath);
    dir.allowExt("mp4");
    dir.allowExt("mov");
    dir.allowExt("avi");
    dir.allowExt("mkv");
    dir.listDir();

    std::unordered_set<std::string> diskFiles;
    for (auto &file : dir)
    {
        diskFiles.insert(file.getFileName());
        if (contents.find(file.getFileName()) == contents.end())
        {
            auto vc = std::make_shared<VideoContent>();
            vc->setup(file.getAbsolutePath());
            registerContent(file.getFileName(), vc);
        }
    }

    for (auto it = contents.begin(); it != contents.end();)
    {
        if (it->first == DEFAULT_CONTENT) { ++it; continue; }
        if (diskFiles.find(it->first) == diskFiles.end()) {
            it = contents.erase(it);
        } else {
            ++it;
        }
    }
}

ofTexture &ContentManager::getTextureById(std::string id)
{
    if (!contents.count(id))
        id = DEFAULT_CONTENT;

    lastUsedFrame[id] = ofGetFrameNum();
    return contents[id]->getTexture();
}

void ContentManager::update()
{
    uint64_t currentFrame = ofGetFrameNum();
    for (auto &kv : contents)
    {
        string id = kv.first;
        if (id == DEFAULT_CONTENT) {
            kv.second->update();
            continue;
        }

        if (currentFrame - lastUsedFrame[id] < 120) {
            kv.second->start();
            kv.second->update();
        } else {
            kv.second->stop();
        }
    }
}
