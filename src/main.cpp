#include "ofMain.h"
#include "ofApp.h"

int main(int argc, char *argv[]) {
    bool headless = false;
    for (int i = 0; i < argc; i++) {
        if (std::string(argv[i]) == "--headless") {
            headless = true;
            break;
        }
    }

    if (headless) {
        // Pure CLI mode: No window, no OpenGL context
        ofApp *app = new ofApp();
        app->bHeadless = true;
        app->setup();
        
        ofLogNotice("System") << "Invasiv running in PURE CLI MODE (Headless)";
        
        while (true) {
            app->update();
            // Sleep to maintain ~60fps logic rate and save CPU
            std::this_thread::sleep_for(std::chrono::microseconds(16666));
        }
        return 0;
    } else {
        ofSetupOpenGL(1024, 768, OF_WINDOW);
        return ofRunApp(new ofApp());
    }
}
