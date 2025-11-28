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
#include "Coms.h"

void Coms::setup(string id)
{
	syncPort = get_free_port();
	uid = id;

	hash_len = uid.length();
	pair = netinfo::preferred_and_broadcast();

	Peer p;
	p.uid = uid;
	p.is_self = true;
	p.ip = pair.preferred;
	p.syncPort = syncPort;
	peers[uid] = p;

	broadcast_uid = std::string(hash_len, '0');

	cout << "my uid : " << uid << " the broadcast adress:" << pair.broadcast << "\n";

	ofxUDPSettings listenerSettings;
	listenerSettings.receiveOn(pair.broadcast, 11999);
	listenerSettings.blocking = false;
	listenerSettings.broadcast = true;
	listenerSettings.reuse = true;

	listener.Setup(listenerSettings);

	ofxUDPSettings senderSettings;
	senderSettings.sendTo(pair.broadcast, 11999);
	senderSettings.blocking = false;
	senderSettings.broadcast = true;
	senderSettings.reuse = true;

	sender.Setup(senderSettings);

	string ipPort = std::format("{}:{}", pair.preferred, syncPort);

	sendBroadcastMessage(CMD_ANNOUNCE, ipPort);
}
uint16_t Coms::getSyncPort()
{
	return syncPort;
};

void Coms::sendMessage(string target_uuid, string command, string message)
{
	string encoded = std::format("{}{}{}{}", uid, target_uuid, command, message);
	sender.Send(encoded.c_str(), encoded.length());
}

void Coms::sendBroadcastMessage(string command, string message)
{
	string encoded = std::format("{}{}{}{}", uid, broadcast_uid, command, message);
	sender.Send(encoded.c_str(), encoded.length());
}

vector<Message> Coms::process()
{
	std::lock_guard<std::mutex> lock(peersMutex);
	char udpMessage[max_message_size];
	vector<Message> new_messages;

	listener.Receive(udpMessage, max_message_size);
	string messageIn = udpMessage;
	if (messageIn != "")
	{
		uint64_t now = ofGetCurrentTime().getAsMilliseconds();

		int index = 0;

		string from_uid = messageIn.substr(index, hash_len);
		index += hash_len;

		if (from_uid != uid)
		{
			Peer p = peers[from_uid];
			p.uid = from_uid;
			p.last_seen = now;
			peers[from_uid] = p;

			std::cout << "seen_peers:\n";
			for (const auto &item : peers)
			{
				std::cout << item.first << ": " << item.second.last_seen << std::endl;
			}

			string target_uid = messageIn.substr(hash_len, hash_len);
			index += hash_len;
			if (target_uid == broadcast_uid || target_uid == uid)
			{
				string command = messageIn.substr(index, 1);
				index += 1;
				string content = messageIn.substr(index);

				std::cout << "processing message from " << from_uid << " to " << target_uid << " : " << content << "\n";
				if (command == CMD_ANNOUNCE)
				{
					parseIpPort(content, p);
					peers[from_uid] = p;

					string reply = std::format("{}:{}", pair.preferred, syncPort);
					sendMessage(from_uid, CMD_ANNOUNCE_REPLY, reply);
				}
				else if (command == CMD_ANNOUNCE_REPLY)
				{
					parseIpPort(content, p);
					peers[from_uid] = p;
				}

				Message m;
				m.from_uid = from_uid;
				m.last_seen = now;
				m.command = command;
				m.content = content;
				new_messages.push_back(m);
			}
		}
	}
	return new_messages;
}
const std::map<std::string, Peer> &Coms::getPeers() const
{
	return peers;
}

// Returns true on success, false on any parse error
bool Coms::parseIpPort(std::string_view input, Peer &out)
{
	// Find the last ':' → works correctly with IPv6 [::1]:8080
	size_t colonPos = input.rfind(':');
	if (colonPos == std::string_view::npos)
	{
		return false;
	}

	// --- Parse port (fastest + safest) ---
	std::string_view portStr = input.substr(colonPos + 1);
	uint16_t port = 0;

	auto [ptr, ec] = std::from_chars(portStr.data(),
									 portStr.data() + portStr.size(),
									 port);

	if (ec != std::errc() || ptr != portStr.data() + portStr.size())
	{
		return false; // invalid number or garbage after digits
	}

	// --- Success: fill the Peer ---
	out.ip = std::string(input.substr(0, colonPos)); // copy IP part
	out.syncPort = port;
	// last_seen remains unchanged (you probably set it yourself)

	return true;
}