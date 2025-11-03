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

struct Message
{
	std::string from_uid;
	uint64_t last_seen;
	std::string content;
};

class Coms
{

public:
	void setup();
	vector<Message> process();
	void sendMessage(string target_uuid, string message);
	void sendBroadcastMessage(string message);

	ofxUDPManager listener;
	ofxUDPManager sender;
	map<string, uint64_t> peers = {};

	string uid;
	netinfo::ip_pair pair;
	int max_message_size = 1024 * 32;
	string broadcast_uid;
	int hash_len;
};
