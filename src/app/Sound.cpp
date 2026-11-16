#include "app/Sound.h"

#include <cstdio>

namespace reaction {

SoundOut::SoundOut(const GameData& data, std::vector<std::filesystem::path> romDirs, const std::string& setting)
    : engine_(std::make_unique<audio::AudioEngine>(data, std::move(romDirs), 48000)) {
    engine_->select(setting);
    std::fprintf(stderr, "sound: %s\n", engine_->status().c_str());
}

SoundOut::~SoundOut() {
    if (dev_) SDL_CloseAudioDevice(dev_);
}

bool SoundOut::open() {
    if (tried_) return dev_ != 0;
    tried_ = true;
    if (!SDL_WasInit(SDL_INIT_AUDIO)) return false;
    SDL_AudioSpec want{}, have{};
    want.freq = 48000;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = &SoundOut::callback;
    want.userdata = this;
    dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!dev_) {
        std::fprintf(stderr, "no audio device: %s\n", SDL_GetError());
        return false;
    }
    engine_->setSampleRate(have.freq);
    SDL_PauseAudioDevice(dev_, 0);
    return true;
}

void SoundOut::callback(void* self, Uint8* stream, int len) {
    static_cast<SoundOut*>(self)->engine_->render(reinterpret_cast<std::int16_t*>(stream), len / 4);
}

}  // namespace reaction
