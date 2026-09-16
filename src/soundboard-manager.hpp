#pragma once

#include <obs-module.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <cstdint>
#include <chrono>

// Per-clip trim + extra-output config.
struct SoundboardClipConfig {
	double startSec = 0.0;    // in-point: seek here on every play()
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

	int httpPort() const { return m_httpPort; }
	void setHttpPort(int p) { m_httpPort = p; }

	// Dock position persistence (stores full QMainWindow state)
	const std::string &dockState() const { return m_dockState; }
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
	// 1.0 = just started, 0.0 = about to stop, -1.0 = not playing / unknown.
	// Uses the trim duration's own countdown when one is set (that's what
	// actually stops the clip), otherwise the media source's real position.
	double remainingFraction(const std::string &sourceName) const;
	// Same underlying countdown as remainingFraction, in seconds. -1.0 if
	// not playing / unknown.
	double remainingSeconds(const std::string &sourceName) const;

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

	// Must be called from obs_module_unload(), not left to the destructor:
	// this instance is a function-local static, so its destructor only runs
	// during the C++ runtime's static-teardown at process exit - on macOS
	// that happens after libobs has already torn itself down, and the
	// libobs calls this used to make here (unregister hotkeys, disconnect
	// signals) segfaulted (SoundboardManager::~SoundboardManager crash,
	// confirmed via a real crash report). obs_module_unload() runs while
	// libobs is still fully valid, so do the libobs-touching cleanup here
	// instead and leave the destructor trivial.
	void Shutdown();

private:
	SoundboardManager();
	~SoundboardManager() = default;

	// Returns the soundboard scene (does NOT addref — caller must not release)
	obs_scene_t *boardScene() const;
	obs_sceneitem_t *findItem(const std::string &sourceName) const;

	void registerHotkeys();
	void unregisterAllHotkeys();
	void connectSceneSignals(obs_source_t *sceneSource);
	void disconnectSceneSignals(obs_source_t *sceneSource);
	std::string filePathFor(const std::string &sourceName) const;
	// Elapsed/total seconds for the clip's current play-through. false if
	// not playing / unknown (elapsedSec/totalSec left untouched).
	bool playbackTimes(const std::string &sourceName, double &elapsedSec, double &totalSec) const;

	static void cbItemAdd(void *data, calldata_t *cd);
	static void cbItemRemove(void *data, calldata_t *cd);
	static void cbHotkeyPlay(void *data, obs_hotkey_id id, obs_hotkey_t *hk, bool pressed);

	struct HotkeyEntry {
		obs_hotkey_id id;
		std::string sourceName;
	};

	bool m_shutdown = false;

	std::string m_sceneName = "Soundboard";
	int m_httpPort = 4489;

	std::string m_dockState;

	std::vector<HotkeyEntry> m_hotkeys;

	std::unordered_map<std::string, SoundboardClipConfig> m_clipConfig;
	// Bumped on every play() so a stale duration-timeout from a prior
	// trigger can't stop a clip that's since been retriggered.
	std::unordered_map<std::string, uint64_t> m_playGen;
	std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_playStart;

	RefreshCallback m_refreshCb;
};
