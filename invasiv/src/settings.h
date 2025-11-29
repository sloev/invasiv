// ConfigSyncedWatcher.h
// Fully working on openFrameworks 0.12.0 – 0.12.1 (Linux/macOS/Windows)

#pragma once
#include <ofMain.h>
#include <filesystem>
#include "MD5.h"
#include "uid.h"
namespace fs = std::filesystem;

class ConfigSyncedWatcher
{
public:
    void setup(std::string_view argName = "--config")
    {
        // --- command line args ---

        workingFolder = fs::current_path();
        workingFolder.make_preferred();

        syncedFolder = workingFolder / "synced";
        videosFolder = workingFolder / "synced" / "videos";
        configsFolder = workingFolder / "synced" / "configs";

        if (!fs::exists(videosFolder))
        {
            fs::create_directories(videosFolder);
        }
        if (!fs::exists(configsFolder))
        {
            fs::create_directories(configsFolder);
        }

        settingsPath = workingFolder / "settings.json";

        loadSettings();
        checkForChanges(); // initial hash
    }

    std::string getID() const
    {
        if (settings.contains("ID") && settings["ID"].is_string())
            return settings["ID"].get<std::string>();
        return "";
    }
    

    void setID(std::string newID)
    {
        if (newID != getID())
        {
            settings["ID"] = std::move(newID);
            saveSettings();
            ofLogNotice("ConfigSyncedWatcher") << "New ID: " << getID();
        }
    }

    ofJson &json() { return settings; }
    const ofJson &json() const { return settings; }

    bool checkForChanges()
    {
        if (!fs::exists(syncedFolder))
        {
            lastHash.clear();
            return false;
        }

        std::string state;
        try
        {
            for (const auto &entry : fs::recursive_directory_iterator(
                     syncedFolder, fs::directory_options::skip_permission_denied))
            {

                if (entry.is_directory())
                    continue;

                auto lwt = entry.last_write_time();
                auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                                lwt.time_since_epoch())
                                .count();

                state += entry.path().string() + "|" +
                         std::to_string(secs) + "|" +
                         std::to_string(entry.file_size()) + "\n";
            }
        }
        catch (...)
        {
            return false;
        }

        std::string newHash = MD5::hash(state);
        bool changed = (!lastHash.empty() && newHash != lastHash);

        if (changed)
        {
            ofLogNotice("ConfigSyncedWatcher") << "Changes detected in synced/ folder!";
        }
        if (newHash != lastHash)
            lastHash = std::move(newHash);

        return changed;
    }

    fs::path getworkingFolder() const { return workingFolder; }
    fs::path getSyncedFolder() const { return syncedFolder; }
    fs::path getConfigsFolder() const { return configsFolder; }
    fs::path getVideosFolder() const { return videosFolder; }
    fs::path getMappingsPathForId(string id) const
    {
        string mappingsFileName = id + ".mappings.json";
        return configsFolder / mappingsFileName;
    }
    fs::path getTextureConfigPath() const
    {
        return configsFolder / "textures.json";
    }

private:
    void loadSettings()
    {
        if (fs::exists(settingsPath))
        {
            std::ifstream ifs(settingsPath);
            ifs >> settings;
        }

        if (!settings.contains("ID") || !settings["ID"].is_string() ||
            settings["ID"].get<std::string>().empty())
        {

            settings["ID"] = short_uid::generate8();
            saveSettings();
            ofLogNotice("ConfigSyncedWatcher") << "Generated new ID: " << getID();
        }
    }

    void saveSettings() const
    {
        std::ofstream ofs(settingsPath);
        ofs << settings.dump(4);
    }

    fs::path workingFolder;
    fs::path syncedFolder;
    fs::path videosFolder;
    fs::path configsFolder;
    fs::path settingsPath;
    ofJson settings;
    std::string lastHash;
};