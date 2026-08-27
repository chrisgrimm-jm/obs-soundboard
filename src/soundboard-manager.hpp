#pragma once

#include <obs-module.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <cstdint>

// Per-clip trim + extra-output config.
struct SoundboardClipConfig {
    double startSec    = 0.0; // in-point: seek here on every play()
    double durationSec = 0.0; // out-point, as a length; 0 = play to natural end
    // Extra system playback devices (by name) this clip should also play out
    // of directly, in addition to always going through the main OBS mix.
    std::vector<std::string> extraOutputDevices;
};

class SoundboardManager {
public:
    static SoundboardManager &instance();

    void loadSettings();
    void saveSettings();

    // Which scene holds the sound-clip sources
    const std::string &sceneName() const { return m_sceneName; }
    void setSceneName(const std::string &name);

    int  httpPort() const   { return m_httpPort; }
    void setHttpPort(int p) { m_httpPort = p; }

    // Dock position persistence (stores full QMainWindow state)
    const std::string &dockState() const        { return m_dockState; }
    void setDockState(const std::string &state) { m_dockState = state; }

    // ── Clip enumeration ─────────────────────────────────────────────────────
    struct ClipInfo {
        std::string sourceName;
    };
    // Snapshot of every source currently in the soundboard scene.
    std::vector<ClipInfo> currentClips() const;

    // ── Playback control ─────────────────────────────────────────────────────
    void play(const std::string &sourceName);
    void stopOne(const std::string &sourceName);
    void stopAll();
    bool isPlaying(const std::string &sourceName) const;

    // ── Per-clip config (trim + extra outputs) ───────────────────────────────
    SoundboardClipConfig clipConfig(const std::string &sourceName) const;
    void setClipConfig(const std::string &sourceName, const SoundboardClipConfig &cfg);

    // ── Setup helper ─────────────────────────────────────────────────────────
    // Creates the soundboard scene if missing, nests it at the top of every
    // other scene in the collection.
    void addToAllScenes();

    // ── UI callback ──────────────────────────────────────────────────────────
    // Fires (queued to Qt main thread by callers) when the clip list changes.
    using RefreshCallback = std::function<void()>;
    void setRefreshCallback(RefreshCallback cb) { m_refreshCb = std::move(cb); }

private:
    SoundboardManager();
    ~SoundboardManager();

    // Returns the soundboard scene (does NOT addref — caller must not release)
    obs_scene_t *boardScene() const;
    obs_sceneitem_t *findItem(const std::string &sourceName) const;

    void registerHotkeys();
    void unregisterAllHotkeys();
    void connectSceneSignals(obs_source_t *sceneSource);
    void disconnectSceneSignals(obs_source_t *sceneSource);
    std::string filePathFor(const std::string &sourceName) const;

    static void cbItemAdd(void *data, calldata_t *cd);
    static void cbItemRemove(void *data, calldata_t *cd);
    static void cbHotkeyPlay(void *data, obs_hotkey_id id, obs_hotkey_t *hk, bool pressed);

    struct HotkeyEntry {
        obs_hotkey_id id;
        std::string   sourceName;
    };

    std::string m_sceneName = "Soundboard";
    int         m_httpPort  = 4489;

    std::string m_dockState;

    std::vector<HotkeyEntry> m_hotkeys;

    std::unordered_map<std::string, SoundboardClipConfig> m_clipConfig;
    // Bumped on every play() so a stale duration-timeout from a prior
    // trigger can't stop a clip that's since been retriggered.
    std::unordered_map<std::string, uint64_t> m_playGen;

    RefreshCallback m_refreshCb;
};
