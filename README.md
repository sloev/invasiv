

attribution:

* loaf_ingredients: MIT -> https://github.com/danomatika/loaf-ingredients
* loaf: GPL v3 -> https://github.com/danomatika/loaf
* openframeworks: MIT -> https://github.com/openframeworks/openFrameworks
* ofxlua: BSD Simplified License -> https://github.com/danomatika/ofxLua




* [x] auto discover via udp
* [x] udp networking via broadcast
* [x] symetric, only one program
* [x] folder sync
* [x] save mapping to json
* [x] shared mapping setup  
* [ ] when set to be master, a program should scan its media folder and offer all the files to all the peers
* [ ] if master and a new peer announces, it should be offered the files
* [ ] if master and mediafolder changes content, master should offer the changed files to peers
* [ ] if in peer mode and  offered a file, check local file and see if it is the same (md5 like) if not, take the offer, overwrite local file

file hirachy

/foo/
    - settings.json
    - mappings.json
    - /synced/
    


I have analyzed your original repository and our discussion. You are correct: hardcoding 255.255.255.255 is often unreliable on complex networks or specific OS configurations (like Linux) because the routing table might not direct it to the correct interface.Using the specific broadcast address of the active interface (e.g., 192.168.1.255) ensures the packet actually leaves the correct network card.Requirements StatusRequirementStatusNoteAuto-DiscoveryFIXEDNow uses getifaddrs to find the real broadcast IP (e.g., 192.168.1.255).NetworkingFIXEDSplit into Sender (Connects to Broadcast) and Listener (Binds 0.0.0.0).Project PathMETUses std::filesystem::current_path to set PWD as root.Config/IdentityMETLoads/Saves config.json and warps.json in PWD/configs/.File SyncMETSmart MD5 syncs media/ folder.UIMETDark theme, Tree View, Target Peer selection.WarpingMETMaster edits specific peer targets; Peers view own content.The ChangesNew File src/IPUtils.h: I ported the logic from your ip.h. It iterates through system interfaces, skips loopback/down interfaces, and finds the first valid broadcast address.Updated src/Network.h: It now calls IPUtils::getBroadcastAddress() and connects the Sender socket to that specific IP instead of 255.255.255.255.File TreePlaintextsrc/
├── Content.h        (Unchanged)
├── IPUtils.h        (NEW: Handles broadcast detection)
├── Identity.h       (Unchanged)
├── Network.h        (CHANGED: Uses IPUtils for broadcast)
├── PacketDef.h      (Unchanged)
├── TinyMD5.h        (Unchanged)
├── WarpController.h (Unchanged)
├── WarpSurface.h    (Unchanged)
├── main.cpp         (Unchanged)
├── ofApp.cpp        (Unchanged)
└── ofApp.h          (Unchanged)
Source Code