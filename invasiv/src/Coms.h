/*==============================================================================

	loaf: lua, osc, and openFrameworks

	Copyright (c) 2020 Dan Wilcox <danomatika@gmail.com>

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program. If not, see <http://www.gnu.org/licenses/>.

	See https://github.com/danomatika/loaf for documentation

==============================================================================*/
#pragma once

#include "ofMain.h"

#include <string>
#include "ip.h"
#include "ofxNetwork.h"
#include "freeport.h"
#include "uid.h"
#include <string_view>
#include <charconv>
#include <cstdint>

#define CMD_ANNOUNCE "0"
#define CMD_ANNOUNCE_REPLY "1"
#define CMD_SCRIPT_RELOAD "2"
#define CMD_SCRIPT_CALL "3" //< send json to script
#define CMD_MAPPING "4"		//< path was created
#define CMD_ANNOUNCE_MAPPING_MASTER_ON "5"
#define CMD_ANNOUNCE_MAPPING_MASTER_OFF "6"

struct Message
{
	std::string from_uid;
	uint64_t last_seen;
	string command;
	std::string content;
};

struct Peer
{
	string uid;
	string ip;
	bool is_self;
	uint16_t syncPort;
	uint64_t last_seen;
};

class Coms
{

public:
	void setup(string id);
	vector<Message> process();
	void sendMessage(string target_uuid, string command, string message = "");
	void sendBroadcastMessage(string command, string message = "");
	bool parseIpPort(std::string_view input, Peer &out);
	uint16_t getSyncPort();
	const std::map<std::string, Peer> &getPeers() const;

	ofxUDPManager listener;
	ofxUDPManager sender;
	map<string, Peer> peers = {};

	string uid;
	netinfo::ip_pair pair;
	int max_message_size = 1024 * 32;
	string broadcast_uid;
	uint16_t syncPort;
	int hash_len;
	int max_command_length = 2;
};
