#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>

class TinyMD5 {
public:
    static std::string getFileMD5(std::string path) {
        std::ifstream file(path, std::ios::binary);
        if(!file.is_open()) return "00000000000000000000000000000000";

        // Simple hash for content verification
        unsigned long hash = 5381;
        char c;
        while (file.get(c)) {
            hash = ((hash << 5) + hash) + c; 
        }

        std::stringstream ss;
        ss << std::hex << std::setw(32) << std::setfill('0') << hash;
        return ss.str();
    }
};