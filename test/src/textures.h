#pragma once
#include "ofMain.h"
#include <map>
#include <string>
#include <memory>
#include <iostream>
class TextureResource
{
public:
    virtual ~TextureResource() = default;
    virtual void start() {}
    virtual void stop() {}
    virtual void update() {}
    virtual ofTexture getTexture() = 0;
};
class DefaultTextureResource : public TextureResource
{
public:
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
    ofTexture getTexture() override
    {
        return tex;
    }
    ofTexture tex;
};
class VideoTextureResource : public TextureResource
{
public:
    void setup(std::string path)
    {
        player.load(path);
    }
    void start() override
    {
        if (!player.isPlaying())
        {
            player.play();
        }
    }
    void stop() override
    {
        if (player.isPlaying())
        {
            player.stop();
        }
    }
    void update() override
    {
        if (player.isPlaying())
        {
            player.update();
        }
    }
    ofTexture getTexture() override
    {
        return player.getTexture();
    }
    ofVideoPlayer player;
};
class TextureManager
{
public:
    void setup()
    {
        auto dtr = std::make_shared<DefaultTextureResource>();
        dtr->setup();
        textures[defaultTextureId] = dtr;
    }
    bool registerTextureResource(std::string id, std::shared_ptr<TextureResource> res)
    {
        if (textures.count(id) > 0)
        {
            return false;
        }
        textures[id] = res;
        return true;
    }
    ofTexture getTextureById(std::string id)
    {
        auto it = textures.find(id);
        if (it != textures.end())
        {
            it->second->start();
            texturesPlaying[id] = true;
            return it->second->getTexture();
        }
        else
        {
            std::cout << "Key "
                         " << id << "
                         " not found"
                      << std::endl;
            return textures[defaultTextureId]->getTexture();
        }
    }

    void update()
    {
        for (const auto &item : textures)
        {
            item.second->update();
        }
    }
    void pauseNotUsedTextures()
    {
        for (const auto &item : textures)
        {
            if (item.first != defaultTextureId && texturesPlaying.count(item.first) == 0)
            {
                item.second->stop();
            }
        }
        texturesPlaying.clear();
    }
    void autoregister()
    {
        
    }

    std::map<std::string, std::shared_ptr<TextureResource>> textures;
    std::map<std::string, bool> texturesPlaying;
    std::string defaultTextureId = "test";
};