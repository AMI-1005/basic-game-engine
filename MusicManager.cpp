#include "MusicManager.hpp"
#include "MusicManager.hpp"
#include "raylib.h"

MusicManager::MusicManager() {
    // Intentionally does NOT call InitAudioDevice(): the audio device is a
    // single shared resource (also used for footstep SFX) and its lifetime
    // is owned by whatever owns this manager (see OverworldState).
}

MusicManager::~MusicManager() {
    UnloadAll();
}

void MusicManager::UnloadAll() {
    for (auto& p : tracks) {
        UnloadMusicStream(p.second);
    }
    tracks.clear();
    hasCurrent = false;
    currentGroup.clear();
}

void MusicManager::RegisterGroupTrack(std::string group, const std::string& filePath) {
    // LoadMusicStream's failure message doesn't distinguish "file not found"
    // from "file found but couldn't be decoded" - check existence first
    // (relative to the process's current working directory, which is often
    // NOT the project folder when running from an IDE) so we know which
    // problem we're actually looking at.
    if (!FileExists(filePath.c_str())) {
        TraceLog(LOG_ERROR, "MusicManager: '%s' not found relative to cwd '%s'",
                 filePath.c_str(), GetWorkingDirectory());
        group = "0";
    }

    Music m = LoadMusicStream(filePath.c_str());
    if (m.stream.buffer != nullptr) {
        m.looping = true; // ambient tracks should loop forever
        tracks[group] = m;
    } else {
        TraceLog(LOG_ERROR, "MusicManager: failed to load '%s'", filePath.c_str());
    }
}

void MusicManager::SwitchToGroup(const std::string& group) {
    // If same group → do nothing
    if (hasCurrent && group == currentGroup) {
        return;
    }

    // Stop old music
    if (hasCurrent) {
        StopMusicStream(tracks[currentGroup]);
    }

    // If group not registered → no music
    if (tracks.find(group) == tracks.end()) {
        TraceLog(LOG_WARNING, "MusicManager: no track registered for group '%s'", group.c_str());
        hasCurrent = false;
        currentGroup.clear();
        return;
    }

    // Start new music
    currentGroup = group;
    hasCurrent = true;
    PlayMusicStream(tracks[group]);
}

void MusicManager::Update() {
    if (hasCurrent) {
        UpdateMusicStream(tracks[currentGroup]);
    }
}
