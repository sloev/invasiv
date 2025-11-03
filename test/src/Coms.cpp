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

void Coms::setup()
{
	uid = short_uid::generate<4>();
	hash_len = uid.length();
	pair = netinfo::preferred_and_broadcast();
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

	sendBroadcastMessage("announce");
}

void Coms::sendMessage(string target_uuid, string message)
{
	string encoded = std::format("{}{}{}", uid, target_uuid, message);
	sender.Send(encoded.c_str(), encoded.length());
}

void Coms::sendBroadcastMessage(string message)
{
	string encoded = std::format("{}{}{}", uid, broadcast_uid, message);
	sender.Send(encoded.c_str(), encoded.length());
}

vector<Message> Coms::process()
{
	char udpMessage[max_message_size];
	vector<Message> new_messages;

	listener.Receive(udpMessage, max_message_size);
	string messageIn = udpMessage;
	if (messageIn != "")
	{
		uint64_t now = ofGetCurrentTime().getAsMilliseconds();

		string from_uid = messageIn.substr(0, hash_len);

		if (from_uid != uid)
		{
			peers[from_uid] = now;
			std::cout << "seen_peers:\n";
			for (const auto &item : peers)
			{
				std::cout << item.first << ": " << item.second << std::endl;
			}

			string target_uid = messageIn.substr(hash_len, hash_len);
			if (target_uid == broadcast_uid || target_uid == uid)
			{
				string content = messageIn.substr(hash_len * 2);

				std::cout << "processing message from " << from_uid << " to " << target_uid << " : " << content << "\n";

				if (content == "announce")
				{
					sendMessage(from_uid, "announce_reply");
				}
				else
				{
					Message m;
					m.from_uid = from_uid;
					m.last_seen = now;
					m.content = content;
					new_messages.push_back(m);
				}
			}
		}
	}
	return new_messages;
}
