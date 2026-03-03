#pragma once
#include "ofMain.h"
#include "ofxNetwork.h"
#include "PacketDef.h"
#include "TinyMD5.h"
#include "IPUtils.h"
#include <queue>
#include <map>

class Network : public ofThread
{
public:
    struct PeerData
    {
        string id;
        AppRole role; 
        float lastSeen;
        bool isSyncing;
        float syncProgress;
        string syncingFile;
    };

    map<string, PeerData> peers;
    
    // State Management
    AppRole role = ROLE_PEER;
    
    string myId;
    string mediaPath;

    ~Network();

    void setup(string _id, string _mediaPath);
    void setupSocket();
    void setMediaPath(string _path);
    void setRole(AppRole newRole);

    bool isAuthority() { return role != ROLE_PEER; }
    bool isEditing() { return role == ROLE_MASTER_EDIT; }
    
    AppRole getMasterRole();
    void setLocalSyncStatus(bool syncing, string filename, float progress);
    bool hasActiveMaster();

    // --- Sending Functions (Wrapped) ---
    void sendHeartbeat();
    void sendWarpMoveAll(string ownerId, int surfIdx, int mode, float dx, float dy);
    void sendWarpScaleAll(string ownerId, int surfIdx, int mode, float factor, float cx, float cy);
    void sendMetronome(float bpm, double refTime, int beats);
    void sendWarp(string ownerId, int surfIdx, int mode, int ptIdx, float x, float y);
    void sendStructure(string jsonStr);
    void offerFile(string filename);

    int receive(char *buf, int max);
    void updatePeers();
    void updatePeer(string id, AppRole role, bool syncing, float progress, string file);

private:
    ofxUDPManager sender;
    ofxUDPManager listener;
    std::queue<string> pendingFiles;

    bool myIsSyncing = false;
    string mySyncFile = "";
    float mySyncProgress = 0.0f;

    // -- Error Handling Vars --
    bool inErrorState = false;
    float errorRecoverTime = 0.0f;
    const float ERROR_BACKOFF_SEC = 2.0f;

    void fillHeader(PacketHeader &h, uint8_t type);
    void sendSafe(const char* data, int size);
    void threadedFunction() override;
    void transferFile(string filename);
};
