#include "soundboard-audio-engine.hpp"

#define MINIAUDIO_IMPLEMENTATION
#include "third_party/miniaudio.h"

#include <obs-module.h>

struct SoundboardAudioEngine::EngineHandle {
    ma_engine engine;
    bool      initialized = false;
};

struct SoundboardAudioEngine::ActiveSound {
    std::string clipKey;
    ma_sound    sound;
};

SoundboardAudioEngine &SoundboardAudioEngine::instance()
{
    static SoundboardAudioEngine inst;
    return inst;
}

SoundboardAudioEngine::SoundboardAudioEngine()
{
    auto *ctx = new ma_context();
    if (ma_context_init(nullptr, 0, nullptr, ctx) != MA_SUCCESS) {
        blog(LOG_WARNING, "[soundboard] Failed to initialize audio context for extra outputs");
        delete ctx;
        ctx = nullptr;
    }
    m_context = ctx;
}

SoundboardAudioEngine::~SoundboardAudioEngine()
{
    stopEverything();

    for (auto &[name, handle] : m_engines) {
        if (handle->initialized) ma_engine_uninit(&handle->engine);
        delete handle;
    }

    if (m_context) {
        auto *ctx = static_cast<ma_context *>(m_context);
        ma_context_uninit(ctx);
        delete ctx;
    }
}

std::vector<SoundboardAudioEngine::Device> SoundboardAudioEngine::listOutputDevices()
{
    std::vector<Device> result;
    auto *ctx = static_cast<ma_context *>(m_context);
    if (!ctx) return result;

    ma_device_info *playbackInfos = nullptr;
    ma_uint32 playbackCount = 0;
    if (ma_context_get_devices(ctx, &playbackInfos, &playbackCount, nullptr, nullptr) != MA_SUCCESS)
        return result;

    for (ma_uint32 i = 0; i < playbackCount; i++)
        result.push_back({playbackInfos[i].name, playbackInfos[i].isDefault != MA_FALSE});

    return result;
}

SoundboardAudioEngine::EngineHandle *SoundboardAudioEngine::engineFor(const std::string &deviceName)
{
    auto it = m_engines.find(deviceName);
    if (it != m_engines.end()) return it->second;

    auto *handle = new EngineHandle();
    auto *ctx = static_cast<ma_context *>(m_context);

    ma_device_id targetId{};
    bool haveId = false;
    if (ctx) {
        ma_device_info *playbackInfos = nullptr;
        ma_uint32 playbackCount = 0;
        if (ma_context_get_devices(ctx, &playbackInfos, &playbackCount, nullptr, nullptr) == MA_SUCCESS) {
            for (ma_uint32 i = 0; i < playbackCount; i++) {
                if (deviceName == playbackInfos[i].name) {
                    targetId = playbackInfos[i].id;
                    haveId = true;
                    break;
                }
            }
        }
    }

    ma_engine_config config = ma_engine_config_init();
    if (haveId) config.pPlaybackDeviceID = &targetId;

    if (ma_engine_init(&config, &handle->engine) == MA_SUCCESS) {
        handle->initialized = true;
    } else {
        blog(LOG_WARNING, "[soundboard] Failed to open audio output device '%s'", deviceName.c_str());
    }

    m_engines[deviceName] = handle;
    return handle;
}

void SoundboardAudioEngine::pruneFinished()
{
    for (auto it = m_activeSounds.begin(); it != m_activeSounds.end();) {
        if (ma_sound_at_end(&(*it)->sound)) {
            ma_sound_uninit(&(*it)->sound);
            delete *it;
            it = m_activeSounds.erase(it);
        } else {
            ++it;
        }
    }
}

void SoundboardAudioEngine::play(const std::string &clipKey,
                                  const std::vector<std::string> &deviceNames,
                                  const std::string &filePath,
                                  double startSec, double durationSec)
{
    if (deviceNames.empty() || filePath.empty()) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    pruneFinished();

    for (const auto &deviceName : deviceNames) {
        EngineHandle *handle = engineFor(deviceName);
        if (!handle->initialized) continue;

        auto *active = new ActiveSound();
        active->clipKey = clipKey;

        ma_result r = ma_sound_init_from_file(&handle->engine, filePath.c_str(),
            MA_SOUND_FLAG_DECODE, nullptr, nullptr, &active->sound);
        if (r != MA_SUCCESS) {
            blog(LOG_WARNING, "[soundboard] Failed to load '%s' for device '%s'",
                 filePath.c_str(), deviceName.c_str());
            delete active;
            continue;
        }

        if (startSec > 0.0) {
            ma_uint32 sr = ma_engine_get_sample_rate(&handle->engine);
            ma_sound_seek_to_pcm_frame(&active->sound, static_cast<ma_uint64>(startSec * sr));
        }

        if (durationSec > 0.0) {
            ma_uint64 nowMs = ma_engine_get_time_in_milliseconds(&handle->engine);
            ma_sound_set_stop_time_in_milliseconds(&active->sound,
                nowMs + static_cast<ma_uint64>(durationSec * 1000));
        }

        ma_sound_start(&active->sound);
        m_activeSounds.push_back(active);
    }
}

void SoundboardAudioEngine::stopAll(const std::string &clipKey)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_activeSounds.begin(); it != m_activeSounds.end();) {
        if ((*it)->clipKey == clipKey) {
            ma_sound_stop(&(*it)->sound);
            ma_sound_uninit(&(*it)->sound);
            delete *it;
            it = m_activeSounds.erase(it);
        } else {
            ++it;
        }
    }
}

void SoundboardAudioEngine::stopEverything()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto *active : m_activeSounds) {
        ma_sound_stop(&active->sound);
        ma_sound_uninit(&active->sound);
        delete active;
    }
    m_activeSounds.clear();
}
