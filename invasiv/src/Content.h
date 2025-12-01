#pragma once
#include "ofMain.h"

class Content {
public:
    ofVideoPlayer video;
    ofFbo testPattern;
    bool showTest = true;
    string mediaPath;

    void setup(string _mediaPath) {
        mediaPath = _mediaPath;
        
        testPattern.allocate(1920, 1080);
        testPattern.begin();
        ofClear(20, 20, 20); 
        ofSetLineWidth(2);
        int step = 100;
        for(int x=0; x<1920; x+=step) {
            for(int y=0; y<1080; y+=step) {
                ofSetColor(255, 0, 100); 
                ofDrawLine(x, 0, x, 1080);
                ofDrawLine(0, y, 1920, y);
                ofSetColor(200);
                ofDrawBitmapString(ofToString(x/step)+","+ofToString(y/step), x+5, y+15);
            }
        }
        testPattern.end();

        string vidFile = ofFilePath::join(mediaPath, "content.mp4");
        // Use absolute path reference
        if(ofFile(vidFile, ofFile::Reference).exists()) {
            video.load(vidFile);
            video.play();
            showTest = false;
        } else {
            showTest = true;
        }
    }

    void update() {
        if(!showTest && video.isLoaded()) video.update();
    }

    ofTexture& getTexture() {
        if(showTest || !video.isLoaded()) return testPattern.getTexture();
        return video.getTexture();
    }
    
    void toggle() {
        showTest = !showTest;
        if(!showTest && video.isLoaded()) video.play();
    }
};