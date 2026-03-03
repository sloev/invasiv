#include "ofMain.h"
#include "ofApp.h"
#include "ofAppNoWindow.h"

//========================================================================
int main(int argc, char *argv[]) {
    bool headless = false;
    for (int i = 0; i < argc; i++) {
        if (std::string(argv[i]) == "--headless") {
            headless = true;
            break;
        }
    }

    if (headless) {
        ofAppNoWindow window;
        ofApp *app = new ofApp();
        app->bHeadless = true;
        ofSetupOpenGL(&window, 1024, 768, OF_WINDOW);
        return ofRunApp(app);
    } else {
        ofSetupOpenGL(1024, 768, OF_WINDOW);
        return ofRunApp(new ofApp());
    }
}
