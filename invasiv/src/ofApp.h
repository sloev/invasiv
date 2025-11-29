#pragma once

#include "ofMain.h"
#include "Coms.h"
#include "sync.h"
#include "warp.h"
#include "ofJson.h"
#include "textures.h"
#include "ofxImGui.h"
#include "settings.h"
#include "freeport.h"
#include "ofWatcher.h"

#define MODE_PERFORM "0"
#define MODE_MAPPING_MASTER "1"
#define MODE_MAPPING_SLAVE "2"

class ofApp : public ofBaseApp
{

public:
	void setup() override;
	void update() override;
	void draw() override;
	void exit() override;

	void keyPressed(int key) override;
	void keyReleased(int key) override;
	void mouseMoved(int x, int y) override;
	void mouseDragged(int x, int y, int button) override;
	void mousePressed(int x, int y, int button) override;
	void mouseReleased(int x, int y, int button) override;
	void mouseScrolled(int x, int y, float scrollX, float scrollY) override;
	void mouseEntered(int x, int y) override;
	void mouseExited(int x, int y) override;
	void windowResized(int w, int h) override;
	void dragEvent(ofDragInfo dragInfo) override;
	void gotMessage(ofMessage msg) override;
void onFilesChanged(std::vector<std::string>& paths);
	void onSyncEvent(SyncStatus &s);
	void afterDraw(ofEventArgs &);
	Coms coms;
	std::unique_ptr<SyncClient> sync;

	std::unique_ptr<Server> target;
	ofWarpStack warpStack;
	TextureManager textureManager;
	string settingsFileName = "settings.json";
	ofxImGui::Gui gui;
	ConfigSyncedWatcher config;
	string selectedPeerId;
	string mode = MODE_PERFORM;
	bool autoSync = false;
	
	float lastSaveTime = 0.0f;	 // <-- add this
	float lastSyncedTime = 0.0f; // <-- add this

	// Variables exposed to ImGui
	float numLines = 10;
	float backGroundColor[3] = {1, 1, 1};
	bool drawLines = false;
	ofWatcher watcher;
};
