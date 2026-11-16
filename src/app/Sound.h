// The SDL side of the sound: one audio device whose callback renders the engine
// (audio/Audio.h). Opening it may fail (no device, a browser waiting for the
// first click or key): the game then runs silently, never blocked.
#pragma once

#include <SDL.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "audio/Audio.h"
#include "core/GameData.h"

namespace reaction {

class SoundOut {
public:
    // The engine starts on `setting` (Settings::sound); the device opens with open().
    SoundOut(const GameData& data, std::vector<std::filesystem::path> romDirs, const std::string& setting);
    ~SoundOut();
    SoundOut(const SoundOut&) = delete;
    SoundOut& operator=(const SoundOut&) = delete;

    // Opens the audio device (once; later calls do nothing). False if it can't.
    bool open();
    bool isOpen() const { return dev_ != 0; }

    audio::AudioEngine& engine() { return *engine_; }

private:
    static void callback(void* self, Uint8* stream, int len);
    std::unique_ptr<audio::AudioEngine> engine_;
    SDL_AudioDeviceID dev_ = 0;
    bool tried_ = false;
};

}  // namespace reaction
