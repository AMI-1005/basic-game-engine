#pragma once
#include "raylib.h"
#include <string>
#include <unordered_map>

class MusicManager {
public:
    MusicManager();
    ~MusicManager();

    // Call every frame
    void Update();

    // Called when a new map is loaded
    void SwitchToGroup(const std::string& group);

    // Register a music track for a group
    void RegisterGroupTrack(std::string group, const std::string& filePath);

    // Stops and frees every registered track. Safe to call more than once.
    // Must be called (directly or via the destructor) BEFORE CloseAudioDevice().
    void UnloadAll();

private:
    std::unordered_map<std::string, Music> tracks;
    std::string currentGroup;
    bool hasCurrent = false;
};
