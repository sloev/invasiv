#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup()
{
    config.setup();                 // supports --config "/path" or -c
    watcher.setCheckInterval(1.0f); // scan once per second



    // Register listener (this is the correct modern way)
    ofAddListener(watcher.filesChanged, this, &ofApp::onFilesChanged);
    watcher.addPath(config.getConfigsFolder());
    watcher.addPath(config.getVideosFolder());
    selectedPeerId = config.getID();

    // --- Create a demo texture (512x512) ---
    textureManager.setup();
    warpStack.loadFromFile(config.getMappingsPathForId(config.getID()));

    gui.setup(nullptr, true);
    gui.afterDraw.add(this, &ofApp::afterDraw, OF_EVENT_ORDER_APP);

    // warpStack.loadFromFile(ofToDataPath(settingsFileName));

    // Add a warp to the stack
    // ofBilinearWarp &warp = warpStack.addWarp();

    // // Get reference to the warp
    // ofBilinearWarp &warp = warpStack.getWarp(warpIndex);

    // Set divisions (e.g., 2 divisions in X and Y, resulting in 3x3 control points)
    // warp.setDivisions(1, 1);

    // // Edit some input control points (normalized 0-1)
    // // By default, they are uniform, but you can adjust
    // warp.setInputPoint(0, 0, ofVec2f(0.2f, 0.2f)); // Top-left
    // warp.setInputPoint(2, 2, ofVec2f(1.0f, 1.0f)); // Bottom-right

    // // Edit some output control points to create perspective distortion (normalized 0-1)
    // warp.setOutputPoint(0, 0, ofVec2f(0.2f, 0.1f)); // Top-left shifted
    // warp.setOutputPoint(2, 0, ofVec2f(0.8f, 0.1f)); // Top-right
    // warp.setOutputPoint(0, 2, ofVec2f(0.1f>>, 0.9f)); // Bottom-left
    // warp.setOutputPoint(2, 2, ofVec2f(0.9f, 0.9f)); // Bottom-right

    coms.setup(config.getID());

    target = std::make_unique<Server>(coms.getSyncPort(), config.getSyncedFolder()); // Use OF data path
    target->startThread();
    

    sync = std::make_unique<SyncClient>(
        config.getSyncedFolder(),
        [this]() -> const auto &
        { return coms.getPeers(); });
    ofAddListener(sync->syncEvent, this, &ofApp::onSyncEvent);
}

void ofApp::onFilesChanged(std::vector<std::string>& paths) {
        ofLogNotice("ofApp") << paths.size() << " file(s) changed in configs folder";
    for (const auto &p : paths)
    {
        ofLogNotice() << "  • " << p;
    }
}

void ofApp::onSyncEvent(SyncStatus &s)
{
    using State = SyncStatus::State;

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
    watcher.update(); // safe to call every frame
    textureManager.update();
    textureManager.pauseNotUsedTextures();
    vector<Message> new_messages = coms.process();
    for (Message m : new_messages)
    {

        if (m.command == CMD_SCRIPT_RELOAD)
        {
            // script reload
        }
        else if (m.command == CMD_ANNOUNCE_MAPPING_MASTER_ON)
        {
            mode = MODE_MAPPING_SLAVE;
        }
        else if (m.command == CMD_ANNOUNCE_MAPPING_MASTER_OFF)
        {
            mode = MODE_PERFORM;
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

    float now = ofGetElapsedTimef(); // seconds since start
    if (mode == MODE_MAPPING_SLAVE && now - lastSaveTime >= 3.0f && config.checkForChanges())
    {
        warpStack.loadFromFile(config.getMappingsPathForId(config.getID()));
        lastSaveTime = now; // update timestamp
    }
}

//--------------------------------------------------------------
void ofApp::draw()
{
    if (mode == MODE_MAPPING_MASTER)
    {
        warpStack.drawEditmode(textureManager);
    }
    else
    {
        warpStack.draw(textureManager);
    }
    // Start drawing to ImGui (newFrame)a
    gui.begin();
    if (mode == MODE_MAPPING_MASTER)
    {

        // Create a new window
        ImGui::Begin("invasiv", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        // ImGui::Checkbox("autoSync", &autoSync);
        if (warpStack.isDirty() && ImGui::Button("sync"))
        {
            warpStack.saveToFile(config.getMappingsPathForId(selectedPeerId));
        }

        ImGui::SeparatorText("Instances");

        string NodeToClose;

        for (const auto &item : coms.peers)
        {
            const string &peerId = item.first;
            const bool isSelf = item.second.is_self;
            const bool isSelected = (selectedPeerId == peerId);

            string title = isSelf ? std::format("[me] {}", peerId) : peerId;

            ImGui::PushID(peerId.c_str());

            // Force open/close state based on selection
            ImGui::SetNextItemOpen(isSelected, ImGuiCond_Always);

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_CollapsingHeader;
            if (isSelected)
                flags |= ImGuiTreeNodeFlags_Selected;

            bool open = ImGui::CollapsingHeader(title.c_str(), flags);

            // Click = select
            if (ImGui::IsItemClicked())
            {
                selectedPeerId = peerId;
                warpStack.loadFromFile(config.getMappingsPathForId(selectedPeerId));
            }

            if (open && isSelected)
            {
                warpStack.drawGui(item.second);
            }

            ImGui::PopID();
        }

        ImGui::End();
    }

    // End our ImGui Frame.
    // From here on, no GUI components can be submitted anymore !
    gui.end();
}

//--------------------------------------------------------------
void ofApp::exit()
{
    target->waitForThread(true);
    sync->stop();
    gui.afterDraw.remove(this, &ofApp::afterDraw, OF_EVENT_ORDER_APP);
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key)
{
    if (key == 'm')
    {
        mode = MODE_MAPPING_MASTER;
        coms.sendBroadcastMessage(CMD_ANNOUNCE_MAPPING_MASTER_ON);
        sync->start();
    }
    if (key == 'p')
    {

        mode = MODE_PERFORM;
        coms.sendBroadcastMessage(CMD_ANNOUNCE_MAPPING_MASTER_OFF);
        sync->stop();
    }
    if (key == 's')
    {
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
    if (mode == MODE_MAPPING_SLAVE)
    {
        ofDrawBitmapStringHighlight(std::format("id: {}", config.getID()), 10, 10);
    }
}