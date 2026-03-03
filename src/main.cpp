#include "ofMain.h"
#include "ofApp.h"

#ifdef HEADLESS_SUPPORT
#include "ofAppNoWindow.h"
#endif

//========================================================================
int main(int argc, char *argv[]) {
    bool headless = false;
    for (int i = 0; i < argc; i++) {
        if (std::string(argv[i]) == "--headless") {
            headless = true;
            break;
        }
    }

#ifdef HEADLESS_SUPPORT
    if (headless) {
        ofAppNoWindow window;
        ofApp *app = new ofApp();
        app->bHeadless = true;
        ofSetupOpenGL(&window, 1024, 768, OF_WINDOW);
        return ofRunApp(app);
    }
#endif

    ofSetupOpenGL(1024, 768, OF_WINDOW);
    return ofRunApp(new ofApp());
}
