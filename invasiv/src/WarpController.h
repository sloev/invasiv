#pragma once
#include "ofMain.h"
#include "WarpSurface.h"
#include "Network.h"

#define DEFAULT_CONTENT "default"

class Content
{

public:
    ofTexture tex;
    void setup()
    {
        ofPixels pix;
        pix.allocate(256, 256, OF_PIXELS_RGB);
        for (int y = 0; y < 256; ++y)
        {
            for (int x = 0; x < 256; ++x)
            {
                // gradient + red grid every 32 px
                ofColor c = ofColor::fromHsb((x / 255.0f) * 255, 200, 255);
                if ((x % 32) < 2 || (y % 32) < 2)
                    c = ofColor::red;
                pix.setColor(x, y, c);
            }
        }
        tex.loadData(pix);
    }

    void start()
    {
    }
    void stop()
    {
    }

    void update()
    {
    }

    ofTexture getTexture()
    {
        return tex;
    }
};

class VideoContent : Content
{
private:
    ofVideoPlayer video;

public:
    void setup(string filename)
    {
        // Use absolute path reference
        if (ofFile(filename, ofFile::Reference).exists())
        {
            video.load(filename);
            video.play();
        }
    }

    void start()
    {
        if (!video.isPlaying())
        {
            video.play();
        }
    }

    void stop()
    {
        if (video.isPlaying())
        {
            video.stop();
        }
    }

    void update()
    {
        if (video.isPlaying())
        {
            video.update();
        }
    }

    ofTexture getTexture()
    {
        if (video.isLoaded())
        {
            return video.getTexture();
        }
        else
        {
            return tex;
        }
    }
};

class ContentManager
{
private:
    std::map<std::string, std::shared_ptr<Content>> contents;
    std::map<std::string, bool> contentPlaying;

public:
    void setup()
    {
        auto dtr = std::make_shared<Content>();
        dtr->setup();
        registerContent(DEFAULT_CONTENT, dtr);
    }
    bool registerContent(std::string id, std::shared_ptr<Content> c)
    {
        if (contents.count(id))
        {
            cout << "cant register content: " << id << " already exists!\n";
            return false;
        }
        cout << "registered content: " << id << "\n";

        contents[id] = c;
        return true;
    }

    ofTexture getTextureById(std::string id)
    {
        if (!contents.count(id))
        {
            id = DEFAULT_CONTENT;
        }

        contents[id]->start();
        contentPlaying[id] = true;
        return contents[id]->getTexture();
    }

    void update()
    {
        for (const auto &item : contents)
        {
            item.second->update();
        }
    }
    void pauseNotUsedTextures()
    {
        for (const auto &item : contents)
        {
            if (item.first != DEFAULT_CONTENT && contentPlaying.count(item.first) == 0)
            {
                item.second->stop();
            }
        }
        contentPlaying.clear();
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

    string myPeerId;
    string targetPeerId;

    void setup(string _savePath, string _myId)
    {
        contents.setup();
        savePath = _savePath;
        myPeerId = _myId;
        targetPeerId = _myId;

        if (ofFile(savePath).exists())
        {
            loadJson(ofBufferFromFile(savePath).getText());
        }

        if (getSurfacesForPeer(myPeerId).empty())
        {
            addLayer(myPeerId, nullptr);
        }
    }

    vector<shared_ptr<WarpSurface>> getSurfacesForPeer(string peerId)
    {
        vector<shared_ptr<WarpSurface>> subset;
        for (auto &s : allSurfaces)
        {
            if (s->ownerId == peerId)
                subset.push_back(s);
        }
        return subset;
    }

    void update()
    {
        contents.update();
    }

    void draw()
    {
        for (auto &s : allSurfaces)
        {
            if (s->ownerId == myPeerId)
            {
                ofTexture tex = contents.getTextureById(s->getContentId());
                s->draw(tex, ofGetWidth(), ofGetHeight());
            }
        }
        contents.pauseNotUsedTextures();
    }

    void drawDebug()
    {
        vector<shared_ptr<WarpSurface>> subset = getSurfacesForPeer(targetPeerId);
        for (auto &s : subset)
        {
            s->drawDebug(ofGetWidth(), ofGetHeight(), editMode);
        }
    }

    void addLayer(string owner, Network *net)
    {
        auto s = make_shared<WarpSurface>(owner);
        allSurfaces.push_back(s);
        vector<shared_ptr<WarpSurface>> subset = getSurfacesForPeer(owner);
        selectedIndex = (int)subset.size() - 1;
        if (net)
            sync(*net);
    }

    void removeLayer(string owner, Network *net)
    {
        vector<shared_ptr<WarpSurface>> subset = getSurfacesForPeer(owner);
        if (subset.empty())
            return;

        if (selectedIndex >= 0 && selectedIndex < (int)subset.size())
        {
            string idToRemove = subset[selectedIndex]->id;
            for (auto it = allSurfaces.begin(); it != allSurfaces.end();)
            {
                if ((*it)->id == idToRemove)
                    it = allSurfaces.erase(it);
                else
                    ++it;
            }
            selectedIndex = max(0, selectedIndex - 1);
            if (net)
                sync(*net);
        }
    }

    void mousePressed(int x, int y, Network &net)
    {
        if (!net.isMaster)
            return;
        vector<shared_ptr<WarpSurface>> subset = getSurfacesForPeer(targetPeerId);
        if (selectedIndex >= 0 && selectedIndex < (int)subset.size())
        {
            int hit = subset[selectedIndex]->getHit(x, y, ofGetWidth(), ofGetHeight(), editMode);
            if (hit != -1)
                subset[selectedIndex]->selectedPoint = hit;
        }
    }

    void mouseDragged(int x, int y, Network &net)
    {
        if (!net.isMaster)
            return;
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
        if (!net.isMaster)
            return;
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
        for (auto &s : allSurfaces)
            groups[s->ownerId].push_back(s->toJson());
        for (auto &kv : groups)
            root["peers"][kv.first] = kv.second;
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
        catch (...)
        {
            ofLogError() << "JSON Parse Error";
        }
    }

    void updatePeerPoint(string owner, int idx, int mode, int pt, float x, float y)
    {
        vector<shared_ptr<WarpSurface>> subset = getSurfacesForPeer(owner);
        if (idx < (int)subset.size())
        {
            subset[idx]->updatePoint(pt, x, y, mode);
        }
    }
};