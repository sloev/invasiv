#include "ofApp.h"

void ofApp::setup(){
	ofBackground(0);

	#ifdef TARGET_RASPBERRY_PI
  	ofSetFullscreen(true);
  #endif
  surfaces = new ofx::piMapper::SurfaceStack();
  surfaceFactory = ofx::piMapper::SurfaceFactory::instance();
ofLog(OF_LOG_NOTICE, "surfacefactory is" + ofToString(surfaceFactory));
 ofx::piMapper::BaseSurface * surface = surfaceFactory->createSurface(ofx::piMapper::SurfaceType::TRIANGLE_SURFACE);
surfaces->push_back(surface);
}

void ofApp::update(){
}

void ofApp::draw(){
	surfaces->draw();
}

void ofApp::keyPressed(int key){
}

void ofApp::keyReleased(int key){
}

void ofApp::mouseDragged(int x, int y, int button){
}

void ofApp::mousePressed(int x, int y, int button){
}

void ofApp::mouseReleased(int x, int y, int button){
}
