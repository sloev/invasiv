#pragma once
#include "ofMain.h"

#define PACKET_ID 0xAA 

enum PacketType : uint8_t {
    PKT_HEARTBEAT  = 1, 
    PKT_WARP_DATA  = 2, 
    PKT_STRUCT     = 3, 
    PKT_FILE_OFFER = 4, 
    PKT_FILE_CHUNK = 5, 
    PKT_FILE_END   = 6
};

enum EditMode : int {
    EDIT_NONE    = 0,
    EDIT_TEXTURE = 1,
    EDIT_MAPPING = 2
};

#pragma pack(push, 1)
struct PacketHeader {
    uint8_t id = PACKET_ID;
    uint8_t type;
    char senderId[9];
};

struct HeartbeatPacket {
    PacketHeader header;
    char peerId[9]; 
    bool isMaster;
    
    // -- NEW: Sync Status --
    bool isSyncing;
    float syncProgress; // 0.0 to 1.0
    char syncingFile[64]; // Truncated filename
};

struct WarpPacket {
    PacketHeader header;
    char ownerId[9];
    uint8_t surfaceIndex;
    uint8_t mode; 
    uint16_t pointIndex;
    float x;
    float y;
};

struct FileOfferPacket {
    PacketHeader header;
    uint32_t totalSize;
    uint16_t nameLen;
    char hash[33]; 
};

struct FileChunkPacket {
    PacketHeader header;
    uint32_t offset;
    uint16_t size;
};
#pragma pack(pop)