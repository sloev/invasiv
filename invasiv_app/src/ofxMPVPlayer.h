#pragma once

#include "ofMain.h"
#include <mpv/client.h>
#include <mpv/render_gl.h>
#include <GLFW/glfw3.h> 

class ofxMPVPlayer : public ofBaseVideoPlayer {
public:
    ofxMPVPlayer() {
        ctx = mpv_create();
        if (!ctx) {
            ofLogError("ofxMPVPlayer") << "Failed to create mpv instance";
            return;
        }

        mpv_set_option_string(ctx, "terminal", "yes");
        mpv_set_option_string(ctx, "msg-level", "all=warn");
        mpv_set_option_string(ctx, "vo", "libmpv");
        
        // DEBUG: Force software decoding to bypass CUDA errors
        mpv_set_option_string(ctx, "hwdec", "no");
        
        mpv_set_option_string(ctx, "loop", "no");

        if (mpv_initialize(ctx) < 0) {
            ofLogError("ofxMPVPlayer") << "Failed to initialize mpv";
        }
    }

    ~ofxMPVPlayer() {
        close();
    }

    bool load(std::string name) override {
        pendingURI = ofToDataPath(name, true);
        bNeedToLoad = true;
        return true; 
    }

    void loadAsync(std::string name) override {
        load(name);
    }

    void close() override {
        if (mpv_gl) {
            mpv_render_context_free(mpv_gl);
            mpv_gl = nullptr;
        }
        if (ctx) {
            mpv_terminate_destroy(ctx);
            ctx = nullptr;
        }
        bLoaded = false;
    }

    void update() override {
        if (!ctx) return;

        // Check for GL Context & Initialize if needed
        if (!mpv_gl) {
            if (glfwGetCurrentContext()) {
                initGL();
            }
        }

        if (!mpv_gl) return;

        // Process Pending Load
        if (bNeedToLoad) {
            const char *cmd[] = {"loadfile", pendingURI.c_str(), NULL};
            if (mpv_command(ctx, cmd) < 0) {
                ofLogError("ofxMPVPlayer") << "Failed to load: " << pendingURI;
            } else {
                bLoaded = true;
                bPaused = false; 
            }
            bNeedToLoad = false;
        }

        // Handle Events
        while (true) {
            mpv_event *event = mpv_wait_event(ctx, 0); 
            if (event->event_id == MPV_EVENT_NONE) break;
            
            if (event->event_id == MPV_EVENT_VIDEO_RECONFIG) {
                long w = 0, h = 0;
                mpv_get_property(ctx, "width", MPV_FORMAT_INT64, &w);
                mpv_get_property(ctx, "height", MPV_FORMAT_INT64, &h);
                
                if (w > 0 && h > 0) {
                    if (!fbo.isAllocated() || fbo.getWidth() != w || fbo.getHeight() != h) {
                        fbo.allocate(w, h, GL_RGB);
                        fbo.getTexture().setTextureMinMagFilter(GL_LINEAR, GL_LINEAR);
                    }
                }
            } 
        }

        // Render
        uint64_t flags = mpv_render_context_update(mpv_gl);
        if (flags & MPV_RENDER_UPDATE_FRAME) {
            renderFrame();
            bFrameNew = true;
        } else {
            bFrameNew = false;
        }
    }

    void play() override {
        if (!ctx) return;
        int flag = 0;
        mpv_set_property(ctx, "pause", MPV_FORMAT_FLAG, &flag);
        bPaused = false;
    }

    void stop() override {
        if (!ctx) return;
        const char *cmd[] = {"stop", NULL};
        mpv_command(ctx, cmd);
        bPaused = true;
    }

    void setPaused(bool bPause) override {
        if (!ctx) return;
        int flag = bPause ? 1 : 0;
        mpv_set_property(ctx, "pause", MPV_FORMAT_FLAG, &flag);
        bPaused = bPause;
    }

    void setPosition(float pct) override {
        if (!ctx) return;
        double duration = getDuration();
        if (duration > 0) {
            double time = duration * pct;
            const char *cmd[] = {"seek", std::to_string(time).c_str(), "absolute", NULL};
            mpv_command(ctx, cmd);
        }
    }

    float getPosition() const override {
        if (!ctx) return 0.0f;
        double pos = 0;
        double dur = getDuration();
        mpv_get_property(ctx, "time-pos", MPV_FORMAT_DOUBLE, &pos);
        if (dur > 0) return (float)(pos / dur);
        return 0.0f;
    }

    float getDuration() const override {
        if (!ctx) return 0.0f;
        double duration = 0;
        mpv_get_property(ctx, "duration", MPV_FORMAT_DOUBLE, &duration);
        return (float)duration;
    }

    void setVolume(float volume) override {
        if (!ctx) return;
        double v = volume * 100.0;
        mpv_set_property(ctx, "volume", MPV_FORMAT_DOUBLE, &v);
    }

    void setLoopState(ofLoopType state) override {
        if (!ctx) return;
        std::string val = (state == OF_LOOP_NONE) ? "no" : "inf";
        mpv_set_option_string(ctx, "loop", val.c_str());
    }

    void setSpeed(float speed) override {
        if (!ctx) return;
        double s = (double)speed;
        mpv_set_property(ctx, "speed", MPV_FORMAT_DOUBLE, &s);
    }

    bool isFrameNew() const override { return bFrameNew; }
    bool isLoaded() const override { return bLoaded; }
    bool isPlaying() const override { return !bPaused; }
    bool isPaused() const override { return bPaused; }

    float getWidth() const override { return fbo.getWidth(); }
    float getHeight() const override { return fbo.getHeight(); }

    void draw(float x, float y, float w, float h) {
        if (fbo.isAllocated()) {
            ofPushStyle();
            ofEnableBlendMode(OF_BLENDMODE_DISABLED);
            fbo.draw(x, y, w, h);
            ofPopStyle();
        }
    }
    
    void draw(float x, float y) {
        draw(x, y, getWidth(), getHeight());
    }

    ofTexture * getTexturePtr() override {
        return fbo.isAllocated() ? &fbo.getTexture() : nullptr;
    }
    
    ofTexture& getTexture() {
        return fbo.getTexture();
    }

    const ofPixels& getPixels() const override {
        static ofPixels dummy;
        return dummy; 
    }
    
    ofPixels& getPixels() override {
        static ofPixels dummy;
        return dummy;
    }

    bool setPixelFormat(ofPixelFormat pixelFormat) override {
        internalPixelFormat = pixelFormat;
        return true;
    }

    ofPixelFormat getPixelFormat() const override {
        return internalPixelFormat;
    }

    mpv_handle* getMPV() { return ctx; }

private:
    mpv_handle *ctx = nullptr;
    mpv_render_context *mpv_gl = nullptr;
    ofFbo fbo;
    bool bLoaded = false;
    bool bPaused = false;
    bool bFrameNew = false;
    
    std::string pendingURI;
    bool bNeedToLoad = false;
    
    ofPixelFormat internalPixelFormat = OF_PIXELS_RGB;

    static void *get_proc_address(void *ctx, const char *name) {
        (void)ctx;
        return (void *)glfwGetProcAddress(name);
    }

    void initGL() {
        if (!glfwGetCurrentContext()) return;

        mpv_opengl_init_params gl_init_params = {
            .get_proc_address = get_proc_address,
            .get_proc_address_ctx = nullptr,
        };

        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_API_TYPE, (void *)MPV_RENDER_API_TYPE_OPENGL},
            {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},
            {MPV_RENDER_PARAM_INVALID, nullptr}
        };

        if (mpv_render_context_create(&mpv_gl, ctx, params) < 0) {
            ofLogError("ofxMPVPlayer") << "Failed to create mpv GL context";
        }
    }

    void renderFrame() {
        if (!fbo.isAllocated()) return;
        
        fbo.begin();
        
        // DEBUG: Clear to RED. 
        // If you see Red: MPV is failing to draw. 
        // If you see Black: MPV is drawing black frames.
        ofClear(255, 0, 0, 255); 
        
        // FIX: Pass the explicit FBO ID (int) instead of 0
        int fbo_id = (int)fbo.getId();
        
        mpv_opengl_fbo mpv_fbo = {
            .fbo = fbo_id, 
            .w = (int)fbo.getWidth(),
            .h = (int)fbo.getHeight(),
            .internal_format = 0 
        };

        int flip_y = 1; 

        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_OPENGL_FBO, &mpv_fbo},
            {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
            {MPV_RENDER_PARAM_INVALID, nullptr}
        };

        mpv_render_context_render(mpv_gl, params);
        fbo.end();
    }
};