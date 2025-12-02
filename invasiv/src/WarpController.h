#pragma once
#include "ofMain.h"
#include "WarpSurface.h"
#include "Network.h"
#include <unordered_set>

#define DEFAULT_CONTENT "default"

class TestTexture
{
public:
    TestTexture(const TestTexture &) = delete;
    void operator=(const TestTexture &) = delete;
    static TestTexture &getInstance()
    {
        static TestTexture instance;
        return instance;
    }
    ofTexture getTexture()
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
    ofTexture tex;
private:
    TestTexture() {}
};

class Content
{
public:
    virtual ~Content() {} 
    virtual void setup() {}
    virtual void start() {}
    virtual void stop() {}
    virtual void update() {}

    // -- FIX: Added 'virtual' keyword here --
    virtual ofTexture getTexture()
    {
        return TestTexture::getInstance().getTexture();
    }
};

class VideoContent : public Content
{
private:
    ofVideoPlayer video;
public:
    void setup(string filename)
    {
        if (ofFile(filename, ofFile::Reference).exists())
        {
            ofLogNotice("VideoContent") << "created video content for " << filename;
            video.load(filename);
            video.setLoopState(OF_LOOP_NORMAL);
            video.play(); 
        }
        else
        {
            ofLogNotice("VideoContent") << "error creating video content for " << filename << " doesnt exist!";
        }
    }

    void start() override
    {
        if (!video.isPlaying()) video.play();
    }

    void stop() override
    {
        if (video.isPlaying()) video.setPaused(true);
    }

    void update() override
    {
        video.update();
    }
    
    ofTexture getTexture() override
    {
        if (video.isInitialized())
        {
            return video.getTexture();
        }
        else
        {
            return TestTexture::getInstance().getTexture();
        }
    }
};

class ContentManager
{
private:
    std::map<std::string, std::shared_ptr<Content>> contents;
    
    std::unordered_set<std::string> activeThisFrame; 
    std::unordered_set<std::string> activeLastFrame; 

public:
    void setup()
    {
        auto dtr = std::make_shared<Content>();
        dtr->setup();
        registerContent(DEFAULT_CONTENT, dtr);
    }

    bool registerContent(std::string id, std::shared_ptr<Content> c)
    {
        if (contents.count(id)) return false;
        ofLogNotice("ContentManager") << "Registered content: " << id;
        contents[id] = c;
        return true;
    }

    void refreshMedia(string mediaPath)
    {
        ofDirectory dir(mediaPath);
        dir.allowExt("mp4"); dir.allowExt("mov"); dir.allowExt("avi"); dir.allowExt("mkv");
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
                ofLogNotice("ContentManager") << "Auto-registered new video: " << file.getFileName();
            }
        }

        for (auto it = contents.begin(); it != contents.end();)
        {
            if (it->first == DEFAULT_CONTENT) { ++it; continue; }
            if (diskFiles.find(it->first) == diskFiles.end())
            {
                ofLogNotice("ContentManager") << "Removing deleted video: " << it->first;
                it = contents.erase(it);
            }
            else { ++it; }
        }
    }

    ofTexture getTextureById(std::string id)
    {
        if (!contents.count(id)) id = DEFAULT_CONTENT;
        activeThisFrame.insert(id);
        return contents[id]->getTexture();
    }

    void update()
    {
        for (const auto &id : activeLastFrame)
        {
            if (contents.count(id)) {
                contents[id]->start();
                contents[id]->update();
            }
        }

        activeLastFrame = activeThisFrame;
        activeThisFrame.clear(); 
        
        for(auto& kv : contents) {
            if(kv.first == DEFAULT_CONTENT) continue;
            if(activeLastFrame.find(kv.first) == activeLastFrame.end()) {
                kv.second->stop();
            }
        }
    }
};

class WarpController
{
public:
    vector<shared_ptr<WarpSurface>> allSurfaces;
    ContentManager contents;
    int selectedIndex = 0;
    int editMode = EDIT_NONE;
    string savePath;
    string mediaPath; 
    string myPeerId;
    string targetPeerId;

    void setup(string _savePath, string _mediaPath, string _myId)
    {
        savePath = _savePath;
        mediaPath = _mediaPath;
        myPeerId = _myId;
        targetPeerId = _myId;

        contents.setup();
        contents.refreshMedia(mediaPath);

        if (ofFile(savePath).exists()) loadJson(ofBufferFromFile(savePath).getText());
        if (getSurfacesForPeer(myPeerId).empty()) addLayer(myPeerId, nullptr);
    }

    void refreshContent() { contents.refreshMedia(mediaPath); }

    vector<shared_ptr<WarpSurface>> getSurfacesForPeer(string peerId)
    {
        vector<shared_ptr<WarpSurface>> subset;
        for (auto &s : allSurfaces) if (s->ownerId == peerId) subset.push_back(s);
        return subset;
    }

    void update() { contents.update(); }

    void draw()
    {
        vector<shared_ptr<WarpSurface>> subset = getSurfacesForPeer(targetPeerId);
        for (size_t i = 0; i < subset.size(); i++)
        {
            ofTexture tex = contents.getTextureById(subset[i]->contentId);
            subset[i]->draw(tex, ofGetWidth(), ofGetHeight());
        }
    }

    void drawDebug()
    {
        vector<shared_ptr<WarpSurface>> subset = getSurfacesForPeer(targetPeerId);
        for (auto &s : subset) s->drawDebug(ofGetWidth(), ofGetHeight(), editMode);
    }

    void addLayer(string owner, Network *net)
    {
        auto s = make_shared<WarpSurface>(owner);
        allSurfaces.push_back(s);
        vector<shared_ptr<WarpSurface>> subset = getSurfacesForPeer(owner);
        selectedIndex = (int)subset.size() - 1;
        if (net) sync(*net);
    }

    void removeLayer(string owner, Network *net)
    {
        vector<shared_ptr<WarpSurface>> subset = getSurfacesForPeer(owner);
        if (subset.empty()) return;
        if (selectedIndex >= 0 && selectedIndex < (int)subset.size())
        {
            string idToRemove = subset[selectedIndex]->id;
            for (auto it = allSurfaces.begin(); it != allSurfaces.end();)
            {
                if ((*it)->id == idToRemove) it = allSurfaces.erase(it);
                else ++it;
            }
            selectedIndex = max(0, selectedIndex - 1);
            if (net) sync(*net);
        }
    }

    void mousePressed(int x, int y, Network &net)
    {
        if (!net.isMaster) return;
        vector<shared_ptr<WarpSurface>> subset = getSurfacesForPeer(targetPeerId);
        if (selectedIndex >= 0 && selectedIndex < (int)subset.size())
        {
            int hit = subset[selectedIndex]->getHit(x, y, ofGetWidth(), ofGetHeight(), editMode);
            if (hit != -1) subset[selectedIndex]->selectedPoint = hit;
        }
    }

    void mouseDragged(int x, int y, Network &net)
    {
        if (!net.isMaster) return;
        vector<shared_ptr<WarpSurface>> subset = getSurfacesForPeer(targetPeerId);
        if (selectedIndex >= 0 && selectedIndex < (int)subset.size())
        {
            auto s = subset[selectedIndex];
            if (s->selectedPoint != -1)
            {
                float nx = ofClamp((float)x / ofGetWidth(), 0, 1);
                float ny = ofClamp((float)y / ofGetHeight(), 0, 1);
                s->updatePoint(s->selectedPoint, nx, ny, editMode);
                net.sendWarp(s->ownerId, selectedIndex, editMode, s->selectedPoint, nx, ny);
            }
        }
    }

    void mouseReleased(Network &net)
    {
        if (!net.isMaster) return;
        vector<shared_ptr<WarpSurface>> subset = getSurfacesForPeer(targetPeerId);
        if (selectedIndex >= 0 && selectedIndex < (int)subset.size())
        {
            if (subset[selectedIndex]->selectedPoint != -1)
            {
                subset[selectedIndex]->selectedPoint = -1;
                sync(net);
            }
        }
    }

    void sync(Network &net)
    {
        ofJson root;
        map<string, ofJson> groups;
        for (auto &s : allSurfaces) groups[s->ownerId].push_back(s->toJson());
        for (auto &kv : groups) root["peers"][kv.first] = kv.second;
        string jStr = root.dump();
        ofSaveJson(savePath, root);
        net.sendStructure(jStr);
    }

    void loadJson(string jStr)
    {
        try
        {
            ofJson root = ofJson::parse(jStr);
            allSurfaces.clear();
            if (root.contains("peers"))
            {
                for (auto &peerItem : root["peers"].items())
                {
                    string owner = peerItem.key();
                    for (auto &layerItem : peerItem.value())
                    {
                        auto s = make_shared<WarpSurface>(owner);
                        s->fromJson(layerItem);
                        allSurfaces.push_back(s);
                    }
                }
            }
            else if (root.contains("layers"))
            {
                for (auto &item : root["layers"])
                {
                    auto s = make_shared<WarpSurface>(myPeerId);
                    s->fromJson(item);
                    allSurfaces.push_back(s);
                }
            }
            selectedIndex = 0;
        }
        catch (...) { ofLogError() << "JSON Parse Error"; }
    }

    void updatePeerPoint(string owner, int idx, int mode, int pt, float x, float y)
    {
        vector<shared_ptr<WarpSurface>> subset = getSurfacesForPeer(owner);
        if (idx < (int)subset.size()) subset[idx]->updatePoint(pt, x, y, mode);
    }
};