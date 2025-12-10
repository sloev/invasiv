![demo time](./codeberg/screencast.gif)

# invasiv

vj and projection mapping software


## attribution:

* openframeworks: MIT -> https://github.com/openframeworks/openFrameworks
* ofxlua: BSD Simplified License -> https://github.com/danomatika/ofxLua

## FEATURES:

* [x] auto discover via udp
* [x] udp networking via broadcast
* [x] symetric, only one program
* [x] media folder sync
* [x] save mapping to json
* [x] shared mapping setup  
* [x] only one program. user decides through button press "m" which instance is the current master on the network)
* [x] uses the broadcast address of the network to communicate over a channel via udp
* [x] has a simple interface that lets you create/delete warp surfaces on all peers from the master
* [x] low latency sync of warp edits from master to peers
* [x] master scans the media folder and syncs changes to peers
* [x] master syncs to new peers when they announce themselves
* [x] peers decides to take offered files and overwrite local ones based on simple md5 hash comparisons
* [x] peers should show in their status ui (upper left corner) whether they are syncing and what they are syncing
* [x] master ui should have a list of files in media folder and whether they have been synced to all peers
* [x] master ui/instances should show wether a peer is synced or its sync progress if possible
* [x] when a new item is added to the media folder i want the content manager in warpcontroller.h to check all the files in 
      mediafolder and create videocontent for them (register) if they dont exist already. and delete the videocontents that are not in media folder.
* [x] when editing textures or mapping, only show the texture being edited
* [x] when editing textures or mapping, allow the user to scale all control points at once
* [x] when editing textures or mapping, allow the user to move all control points at once
* [x] surfaces should be reorderable
* [x] master should be able to select what content for each surface
* [x] increment or decrement divisions for horisontal/vertical for each surface
* [x] reset edit_mode in warp controller on change of surface etc
* [x] use more efficient double mesh (one for control, one for render)
* [ ] fix bug with shutdown and freeze (introduced after more efficient double mesh)
* [ ] make the other surfaces visible, but 50% transparent during editing of a surface
* [ ] introduce a new live_master mode for controlling all the instances and surfaces

