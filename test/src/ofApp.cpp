#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup()
{
    coms.setup();

    target = std::make_unique<tcp_file::Server>(8081, ofToDataPath("target")); // Use OF data path
    target->startThread();

    // tcp_file::Client cli("127.0.0.1", 8080);
    // cli.connect();
    // std::cout << cli.list();           // remote dir listing
    // cli.download("remote.txt", "local.txt");
    // cli.upload("myfile.bin", "remote.bin");
    std::vector<tcp_file::SyncClient::Target> targets = {
        {"127.0.0.1", 8081}
    };
    sync = std::make_unique<tcp_file::SyncClient>(targets, ofToDataPath("shared"));
    ofAddListener(sync->syncEvent, this, &ofApp::onSyncEvent);
    sync->startThread();
}

void ofApp::onSyncEvent(tcp_file::SyncStatus& s) {
    using State = tcp_file::SyncStatus::State;

    std::string stateStr;
    std::string color = "";  // optional: for colored output in terminal (if supported)

    switch (s.state) {
        case State::Connecting: stateStr = "CONNECTING"; break;
        case State::Listing:    stateStr = "LISTING";    break;
        case State::Uploading:  stateStr = "UPLOAD";     break;
        case State::Deleting:   stateStr = "DELETE";     break;
        case State::Done:       stateStr = "DONE";       break;
        case State::Error:      stateStr = "ERROR";      color = "\033[31m"; break; // red
        default:                stateStr = "UNKNOWN";    break;
    }

    std::ostringstream oss;
    oss << "[" << s.host << ":" << s.port << "] "
        << stateStr << " " << s.filename;

    // Add progress for uploads
    if (s.state == State::Uploading && s.total > 0) {
        float pct = s.percent() * 100.0f;
        oss << " [" << s.bytes << "/" << s.total << " bytes] "
            << ofToString(pct, 1) << "%";
    }

    // Add message (e.g. "Connected", "Failed to connect")
    if (!s.message.empty()) {
        oss << " — " << s.message;
    }

    // Reset color if used
    std::string reset = color.empty() ? "" : "\033[0m";

    ofLogNotice("Sync") << color << oss.str() << reset;
}
//--------------------------------------------------------------
void ofApp::update()
{
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
