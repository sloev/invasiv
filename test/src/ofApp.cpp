#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup()
{
    config.setup(); // supports --config "/path" or -c

    // --- Create a demo texture (512x512) ---
    textureManager.setup();
    warpStack.loadFromFile(config.getMappingsPathForId(config.getID()));

    gui.setup(nullptr, true);
    gui.afterDraw.add(this, &ofApp::afterDraw, OF_EVENT_ORDER_APP);

    // warpStack.loadFromFile(ofToDataPath(settingsFileName));

    // Add a warp to the stack
    ofBilinearWarp &warp = warpStack.addWarp();

    // // Get reference to the warp
    // ofBilinearWarp &warp = warpStack.getWarp(warpIndex);

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

    coms.setup(config.getID());

    target = std::make_unique<tcp_file::Server>(coms.getSyncPort(), config.getSyncedFolder()); // Use OF data path
    target->startThread();

    // ---- sync client (master) ----
    sync = std::make_unique<tcp_file::SyncClient>(config.getSyncedFolder());
    ofAddListener(sync->syncEvent, this, &ofApp::onSyncEvent);
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
    textureManager.update();
    textureManager.pauseNotUsedTextures();
    vector<Message> new_messages = coms.process();
    for (Message m : new_messages)
    {
        if (m.command == CMD_SCRIPT_RELOAD)
        {
            // script reload
        }
        else if (m.command == CMD_MAPPING)
        {
            try
            {
                ofJson json;
                json = ofJson::parse(m.content);
                warpStack.fromJson(json);
            }
            catch (const std::exception &e)
            {
                ofLogError() << "JSON parse error: " << e.what();
            }
        }
        else if (m.command == CMD_SCRIPT_CALL)
        {
            // send message on to script
        }
    }

    int numLinesCopy = numLines; // do something with numLines...
    numLinesCopy *= 1;           // silence "unused variable" warning !
}

//--------------------------------------------------------------
void ofApp::draw()
{
    warpStack.drawAll(textureManager);
    // Start drawing to ImGui (newFrame)a
    gui.begin();

    // Create a new window
    ImGui::Begin("invasiv");
    for (const auto &item : coms.peers)
    {

        if (ImGui::CollapsingHeader(std::format("peer: {}", item.first).c_str()))
        {
            ImGui::TextWrapped("This example can be compiled with or without `USE_AUTODRAW`, to demonstrate 2 different behaviours.");
        }
    }

    ImGui::End();

    // End our ImGui Frame.
    // From here on, no GUI components can be submitted anymore !
    gui.end();
}

//--------------------------------------------------------------
void ofApp::exit()
{
    target->waitForThread(true);
    sync->waitForThread(true);
    gui.afterDraw.remove(this, &ofApp::afterDraw, OF_EVENT_ORDER_APP);
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key)
{
    if (key == 'w')
    {
        sync->waitForThread(true);

        sync->syncToPeers(coms.peers);
    } // Save to file on 's'
    if (key == 's')
        warpStack.saveToFile(config.getMappingsPathForId(config.getID()));
        ofLogNotice() << "Saved warp settings to warp_settings.json";
    }

    // Load from file on 'l'
    if (key == 'l')
    {
        warpStack.loadFromFile(config.getMappingsPathForId(config.getID()));
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

void ofApp::afterDraw(ofEventArgs &)
{
    ofDrawBitmapStringHighlight("The gui.afterDraw() event always lets you draw above the GUI.", 10, 45);
}