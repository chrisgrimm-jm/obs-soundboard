#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

// Plays clips directly out of arbitrary system output devices, independent of
// (and in addition to) whatever OBS itself does with a source's own Media
// Source playback. OBS's own audio pipeline only supports ONE shared
// "Monitoring Device" app-wide — this exists because a soundboard clip may
// need to go out to several physical devices at once (main speakers,
// headphones, an external monitor, etc.), which OBS's mixer has no concept
// of. Backed by miniaudio; kept out of this header so nothing else in the
// plugin needs to know that.
class SoundboardAudioEngine {
public:
	static SoundboardAudioEngine &instance();

	struct Device {
		std::string name;
		bool isDefault = false;
	};
	// Current system playback devices, freshly enumerated each call.
	std::vector<Device> listOutputDevices();

	// Plays filePath on every named device, trimmed to
	// [startSec, startSec + durationSec) (durationSec <= 0 plays to the
	// file's natural end). clipKey identifies the OBS source this came from,
	// so stopAll(clipKey) can stop just its instances.
	void play(const std::string &clipKey, const std::vector<std::string> &deviceNames, const std::string &filePath,
		  double startSec, double durationSec);

	void stopAll(const std::string &clipKey);
	void stopEverything();

private:
	SoundboardAudioEngine();
	~SoundboardAudioEngine();
	SoundboardAudioEngine(const SoundboardAudioEngine &) = delete;

	struct EngineHandle;
	struct ActiveSound;

	EngineHandle *engineFor(const std::string &deviceName);
	// Caller must hold m_mutex.
	void pruneFinished();

	void *m_context = nullptr; // ma_context*

	std::unordered_map<std::string, EngineHandle *> m_engines;
	std::vector<ActiveSound *> m_activeSounds;
	std::mutex m_mutex;
};
