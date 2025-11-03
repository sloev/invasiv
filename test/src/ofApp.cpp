#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup()
{
    // --- Create a demo texture (512x512) ---

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
    demoTexture.loadData(pix);
    myPlayer.load("fluk.mp4");
myPlayer.play();

    // Add a warp to the stack
    size_t warpIndex = warpStack.addWarp();

    // Get reference to the warp
    ofBilinearWarp &warp = warpStack.getWarp(warpIndex);

    // Set divisions (e.g., 2 divisions in X and Y, resulting in 3x3 control points)
    warp.setDivisions(1, 1);

    // // Edit some input control points (normalized 0-1)
    // // By default, they are uniform, but you can adjust
    warp.setInputPoint(0, 0, ofVec2f(0.2f, 0.2f)); // Top-left
    // warp.setInputPoint(2, 2, ofVec2f(1.0f, 1.0f)); // Bottom-right

    // // Edit some output control points to create perspective distortion (normalized 0-1)
    // warp.setOutputPoint(0, 0, ofVec2f(0.2f, 0.1f)); // Top-left shifted
    // warp.setOutputPoint(2, 0, ofVec2f(0.8f, 0.1f)); // Top-right
    // warp.setOutputPoint(0, 2, ofVec2f(0.1f, 0.9f)); // Bottom-left
    // warp.setOutputPoint(2, 2, ofVec2f(0.9f, 0.9f)); // Bottom-right

    coms.setup();

    target = std::make_unique<tcp_file::Server>(8081, ofToDataPath("target")); // Use OF data path
    target->startThread();

    // tcp_file::Client cli("127.0.0.1", 8080);
    // cli.connect();
    // std::cout << cli.list();           // remote dir listing
    // cli.download("remote.txt", "local.txt");
    // cli.upload("myfile.bin", "remote.bin");
    std::vector<tcp_file::SyncClient::Target> targets = {
        {"127.0.0.1", 8081}};
    sync = std::make_unique<tcp_file::SyncClient>(targets, ofToDataPath("shared"));
    ofAddListener(sync->syncEvent, this, &ofApp::onSyncEvent);
    sync->startThread();
}

void ofApp::onSyncEvent(tcp_file::SyncStatus &s)
{
    using State = tcp_file::SyncStatus::State;

    std::string stateStr;
    std::string color = ""; // optional: for colored output in terminal (if supported)

    switch (s.state)
    {
    case State::Connecting:
        stateStr = "CONNECTING";
        break;
    case State::Listing:
        stateStr = "LISTING";
        break;
    case State::Uploading:
        stateStr = "UPLOAD";
        break;
    case State::Deleting:
        stateStr = "DELETE";
        break;
    case State::Done:
        stateStr = "DONE";
        break;
    case State::Error:
        stateStr = "ERROR";
        color = "\033[31m";
        break; // red
    default:
        stateStr = "UNKNOWN";
        break;
    }

    std::ostringstream oss;
    oss << "[" << s.host << ":" << s.port << "] "
        << stateStr << " " << s.filename;

    // Add progress for uploads
    if (s.state == State::Uploading && s.total > 0)
    {
        float pct = s.percent() * 100.0f;
        oss << " [" << s.bytes << "/" << s.total << " bytes] "
            << ofToString(pct, 1) << "%";
    }

    // Add message (e.g. "Connected", "Failed to connect")
    if (!s.message.empty())
    {
        oss << " — " << s.message;
    }

    // Reset color if used
    std::string reset = color.empty() ? "" : "\033[0m";

    ofLogNotice("Sync") << color << oss.str() << reset;
}
//--------------------------------------------------------------
void ofApp::update()
{
        myPlayer.update(); // get all the new frames

    vector<Message> new_messages = coms.process();
    for (Message m : new_messages)
    {
        if (m.content == "reload")
        {
            // script reload
        }
        else
        {
            // send message on to script
        }
    }
}

//--------------------------------------------------------------
void ofApp::draw()
{
    warpStack.drawAll(myPlayer.getTexture());
    demoTexture.draw(0, 0);
}

//--------------------------------------------------------------
void ofApp::exit()
{
    target->stopThread();
    sync->stopThread();
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key)
{
    ofBilinearWarp& warp = warpStack.getWarp(0);  // Assuming one warp

        // Add/remove divisions
        if (key == 'x') warp.addDivisionX();
        if (key == 'X') warp.removeDivisionX();
        if (key == 'y') warp.addDivisionY();
        if (key == 'Y') warp.removeDivisionY();

        // Save to file on 's'
        if (key == 's') {
            warpStack.saveToFile("warp_settings.json");
            ofLogNotice() << "Saved warp settings to warp_settings.json";
        }

        // Load from file on 'l'
        if (key == 'l') {
            warpStack.loadFromFile("warp_settings.json");
            ofLogNotice() << "Loaded warp settings from warp_settings.json";
        }
}

//--------------------------------------------------------------
void ofApp::keyReleased(int key)
{
}

//--------------------------------------------------------------
void ofApp::mouseMoved(int x, int y)
{
}

//--------------------------------------------------------------
void ofApp::mouseDragged(int x, int y, int button)
{
}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button)
{
}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button)
{
}

//--------------------------------------------------------------
void ofApp::mouseScrolled(int x, int y, float scrollX, float scrollY)
{
}

//--------------------------------------------------------------
void ofApp::mouseEntered(int x, int y)
{
}

//--------------------------------------------------------------
void ofApp::mouseExited(int x, int y)
{
}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h)
{
}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg)
{
}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo)
{
}
