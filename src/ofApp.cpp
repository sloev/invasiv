#include "ofApp.h"

#ifndef VERSION_NAME
#define VERSION_NAME "dev"
#endif

void ofApp::setup() {
    core.setup(bHeadless);
    ofSetFrameRate(60);

    // Setup Audio Input for Beat Tracker
    ofSoundStreamSettings settings;
    settings.numOutputChannels = 0;
    settings.numInputChannels = 1;
    settings.sampleRate = 22050; // Required by BeatNet
    settings.bufferSize = 512;
    settings.numBuffers = 4;
    settings.setInListener(this);
    soundStream.setup(settings);

    if (!bHeadless) {        ofSetVerticalSync(true);
        ofBackground(20);
        ofSetWindowTitle("invasiv " + string(VERSION_NAME));
        gui.setup();
        
        if (core.projectPath == "" || !ofDirectory(core.projectPath).exists()) {
            ofFileDialogResult res = ofSystemLoadDialog("Select Invasiv Project Folder", true);
            if (res.bSuccess) {
                core.saveSettings(res.getPath());
                core.reloadProject(res.getPath());
            }
        }
    }
    
    strncpy(pathInputBuf, core.projectPath.c_str(), 255);
    ofAddListener(core.watcher.filesChanged, this, &ofApp::onFilesChanged);
}

void ofApp::update() {
    core.update();
    if (helpTimer > 0) helpTimer -= ofGetLastFrameTime();
}

void ofApp::onFilesChanged(std::vector<std::string> &files) {
    core.onFilesChanged(files);
}

void ofApp::draw() {
    if (bHeadless) return;

    if (core.net.isAuthority()) {
        if (core.net.isEditing()) core.warper.drawDebug();
        else ofDrawBitmapStringHighlight("MASTER: PERFORMANCE MODE", 10, 20, ofColor::black, ofColor::green);

        AppComponents components = {
            core.identity, core.net, core.warper, core.watcher,
            core.stateMgr, core.metro, core.tracker, pathInputBuf, core.projectPath, core
        };

        gui.draw(components);
    } else {
        core.warper.draw();
        if (core.net.getMasterRole() == ROLE_MASTER_EDIT) {
            ofDrawBitmapStringHighlight("Role: PEER | ID: " + core.identity.myId, 10, 20);
            if (core.incoming.active) {
                float pct = (float)core.incoming.current / core.incoming.total * 100.0;
                ofDrawBitmapStringHighlight("Syncing " + core.incoming.name + ": " + ofToString(pct, 1) + "%", 10, 40);
                ofPushStyle();
                ofNoFill(); ofSetColor(255); ofDrawRectangle(10, 50, 200, 10);
                ofFill(); ofSetColor(0, 255, 0); ofDrawRectangle(10, 50, 200 * (pct / 100.0), 10);
                ofPopStyle();
            }
        }
    }

    if (bShowHelp || helpTimer > 0) {
        string help = "INVASIV // QUICK HELP\n\n";
        help += "[m] - Master Edit Mode\n";
        help += "[n] - Master Perform Mode\n";
        help += "[p] - Peer Mode\n";
        help += "[f] - Toggle Fullscreen\n";
        help += "[h] - Toggle This Help\n\n";
        help += "Support development: invasiv.github.io";
        
        ofDrawBitmapStringHighlight(help, ofGetWidth() - 250, 20, ofColor::black, ofColor::yellow);
    }
}

void ofApp::mousePressed(int x, int y, int button) {
    if (bHeadless || !core.net.isEditing() || ImGui::GetIO().WantCaptureMouse) return;
    core.warper.mousePressed(x, y, core.net);
}

void ofApp::mouseDragged(int x, int y, int button) {
    if (bHeadless || !core.net.isEditing() || ImGui::GetIO().WantCaptureMouse) return;
    core.warper.mouseDragged(x, y, core.net);
}

void ofApp::mouseReleased(int x, int y, int button) {
    if (!bHeadless && core.net.isEditing()) core.warper.mouseReleased(core.net);
}

void ofApp::keyPressed(int key) {
    if (!core.net.isEditing()) core.stateMgr.processKey(key, core.warper, core.net);
    if (key == 'f') core.identity.toggleFullscreen();
    if (key == 'h') { bShowHelp = !bShowHelp; helpTimer = 0; }
    if (key == 'm') { core.warper.reset(); core.net.setRole(ROLE_MASTER_EDIT); core.syncFullState(); }
    if (key == 'n') { core.warper.reset(); core.net.setRole(ROLE_MASTER_PERFORM); core.syncFullState(); }
    if (key == 'p') { core.warper.reset(); core.warper.targetPeerId = core.identity.myId; core.net.setRole(ROLE_PEER); }
}

void ofApp::exit() {
    ofRemoveListener(core.watcher.filesChanged, this, &ofApp::onFilesChanged);
    core.exit();
}

void ofApp::audioIn(ofSoundBuffer & input) {
    core.tracker.audioIn(input);
}
