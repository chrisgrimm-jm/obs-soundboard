#include "soundboard-manager.hpp"
#include "soundboard-audio-engine.hpp"

#include <obs-frontend-api.h>
#include <util/platform.h>
#include <callback/signal.h>

#include <QCoreApplication>
#include <QMetaObject>
#include <QTimer>

#include <algorithm>
#include <cstring>

// ── Singleton ─────────────────────────────────────────────────────────────────

SoundboardManager &SoundboardManager::instance()
{
	static SoundboardManager inst;
	return inst;
}

SoundboardManager::SoundboardManager() {}

void SoundboardManager::Shutdown()
{
	if (m_shutdown)
		return;
	m_shutdown = true;

	unregisterAllHotkeys();

	obs_source_t *src = obs_get_source_by_name(m_sceneName.c_str());
	if (src) {
		disconnectSceneSignals(src);
		obs_source_release(src);
	}
}

// ── Scene access ──────────────────────────────────────────────────────────────

obs_scene_t *SoundboardManager::boardScene() const
{
	obs_source_t *src = obs_get_source_by_name(m_sceneName.c_str());
	if (!src)
		return nullptr;
	obs_scene_t *scene = obs_scene_from_source(src);
	obs_source_release(src);
	return scene;
}

obs_sceneitem_t *SoundboardManager::findItem(const std::string &sourceName) const
{
	obs_scene_t *scene = boardScene();
	if (!scene)
		return nullptr;
	return obs_scene_find_source(scene, sourceName.c_str());
}

// ── Scene name change ─────────────────────────────────────────────────────────

void SoundboardManager::setSceneName(const std::string &name)
{
	if (name == m_sceneName)
		return;

	obs_source_t *old = obs_get_source_by_name(m_sceneName.c_str());
	if (old) {
		disconnectSceneSignals(old);
		obs_source_release(old);
	}
	unregisterAllHotkeys();

	m_sceneName = name;

	obs_source_t *next = obs_get_source_by_name(m_sceneName.c_str());
	if (next) {
		connectSceneSignals(next);
		obs_source_release(next);
	}
	registerHotkeys();

	if (m_refreshCb)
		m_refreshCb();
}

// ── Clip enumeration ─────────────────────────────────────────────────────────

std::vector<SoundboardManager::ClipInfo> SoundboardManager::currentClips() const
{
	std::vector<ClipInfo> result;
	obs_scene_t *scene = boardScene();
	if (!scene)
		return result;

	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) -> bool {
			auto *out = static_cast<std::vector<ClipInfo> *>(param);
			obs_source_t *src = obs_sceneitem_get_source(item);
			if (src) {
				const char *name = obs_source_get_name(src);
				if (name && *name)
					out->push_back({name});
			}
			return true;
		},
		&result);

	return result;
}

// ── Playback control ─────────────────────────────────────────────────────────

void SoundboardManager::play(const std::string &sourceName)
{
	obs_sceneitem_t *item = findItem(sourceName);
	if (!item)
		return;
	obs_source_t *src = obs_sceneitem_get_source(item);
	if (!src)
		return;

	obs_source_media_restart(src);
	m_playStart[sourceName] = std::chrono::steady_clock::now();

	auto it = m_clipConfig.find(sourceName);
	if (it == m_clipConfig.end())
		return;
	const SoundboardClipConfig &cfg = it->second;

	if (cfg.startSec > 0.0)
		obs_source_media_set_time(src, static_cast<int64_t>(cfg.startSec * 1000));

	if (!cfg.extraOutputDevices.empty()) {
		std::string filePath = filePathFor(sourceName);
		SoundboardAudioEngine::instance().play(sourceName, cfg.extraOutputDevices, filePath, cfg.startSec,
						       cfg.durationSec);
	}

	if (cfg.durationSec > 0.0) {
		uint64_t gen = ++m_playGen[sourceName];
		std::string name = sourceName;
		int ms = static_cast<int>(cfg.durationSec * 1000);
		// Hop onto the Qt main thread first: play() can be called from the
		// OBS hotkey-dispatch thread, which doesn't pump a Qt event loop, so
		// a QTimer started there would never fire.
		QMetaObject::invokeMethod(
			qApp,
			[this, name, gen, ms]() {
				QTimer::singleShot(ms, [this, name, gen]() {
					if (m_playGen[name] == gen)
						stopOne(name);
				});
			},
			Qt::QueuedConnection);
	}
}

void SoundboardManager::stopOne(const std::string &sourceName)
{
	obs_sceneitem_t *item = findItem(sourceName);
	if (item) {
		obs_source_t *src = obs_sceneitem_get_source(item);
		if (src)
			obs_source_media_stop(src);
	}
	SoundboardAudioEngine::instance().stopAll(sourceName);
}

void SoundboardManager::stopAll()
{
	for (const auto &clip : currentClips())
		stopOne(clip.sourceName);
}

bool SoundboardManager::isPlaying(const std::string &sourceName) const
{
	obs_sceneitem_t *item = findItem(sourceName);
	if (!item)
		return false;
	obs_source_t *src = obs_sceneitem_get_source(item);
	if (!src)
		return false;
	return obs_source_media_get_state(src) == OBS_MEDIA_STATE_PLAYING;
}

bool SoundboardManager::playbackTimes(const std::string &sourceName, double &elapsedSec, double &totalSec) const
{
	if (!isPlaying(sourceName))
		return false;

	auto cfgIt = m_clipConfig.find(sourceName);
	double durationSec = (cfgIt != m_clipConfig.end()) ? cfgIt->second.durationSec : 0.0;

	// A trim duration is enforced by play()'s own QTimer, not by the media
	// source itself, so that's the countdown that actually matters here.
	if (durationSec > 0.0) {
		auto startIt = m_playStart.find(sourceName);
		if (startIt == m_playStart.end())
			return false;
		elapsedSec = std::chrono::duration<double>(std::chrono::steady_clock::now() - startIt->second).count();
		totalSec = durationSec;
		return true;
	}

	obs_sceneitem_t *item = findItem(sourceName);
	obs_source_t *src = item ? obs_sceneitem_get_source(item) : nullptr;
	if (!src)
		return false;

	int64_t totalMs = obs_source_media_get_duration(src);
	int64_t curMs = obs_source_media_get_time(src);
	if (totalMs <= 0)
		return false;

	elapsedSec = double(curMs) / 1000.0;
	totalSec = double(totalMs) / 1000.0;
	return true;
}

double SoundboardManager::remainingFraction(const std::string &sourceName) const
{
	double elapsedSec, totalSec;
	if (!playbackTimes(sourceName, elapsedSec, totalSec) || totalSec <= 0.0)
		return -1.0;
	return std::clamp(1.0 - elapsedSec / totalSec, 0.0, 1.0);
}

double SoundboardManager::remainingSeconds(const std::string &sourceName) const
{
	double elapsedSec, totalSec;
	if (!playbackTimes(sourceName, elapsedSec, totalSec))
		return -1.0;
	return std::clamp(totalSec - elapsedSec, 0.0, totalSec);
}

// ── Per-clip config ────────────────────────────────────────────────────────────

SoundboardClipConfig SoundboardManager::clipConfig(const std::string &sourceName) const
{
	auto it = m_clipConfig.find(sourceName);
	return it != m_clipConfig.end() ? it->second : SoundboardClipConfig{};
}

void SoundboardManager::setClipConfig(const std::string &sourceName, const SoundboardClipConfig &cfg)
{
	m_clipConfig[sourceName] = cfg;
}

// The file path OBS's own Media Source is playing, so the extra-output audio
// engine can play the exact same file independently. "local_file" is the
// property id OBS's built-in ffmpeg_source (Media Source) stores it under.
std::string SoundboardManager::filePathFor(const std::string &sourceName) const
{
	obs_sceneitem_t *item = findItem(sourceName);
	if (!item)
		return {};
	obs_source_t *src = obs_sceneitem_get_source(item);
	if (!src)
		return {};

	obs_data_t *settings = obs_source_get_settings(src);
	const char *path = obs_data_get_string(settings, "local_file");
	std::string result = path ? path : "";
	obs_data_release(settings);
	return result;
}

// ── Setup helper ──────────────────────────────────────────────────────────────

void SoundboardManager::addToAllScenes()
{
	obs_source_t *boardSrc = obs_get_source_by_name(m_sceneName.c_str());
	if (!boardSrc) {
		obs_scene_t *newScene = obs_scene_create(m_sceneName.c_str());
		if (!newScene)
			return;
		boardSrc = obs_source_get_ref(obs_scene_get_source(newScene));
	}

	struct obs_frontend_source_list list = {};
	obs_frontend_get_scenes(&list);

	for (size_t i = 0; i < list.sources.num; i++) {
		obs_source_t *sceneSrc = list.sources.array[i];
		const char *sceneName = obs_source_get_name(sceneSrc);
		if (sceneName && strcmp(sceneName, m_sceneName.c_str()) == 0)
			continue;

		obs_scene_t *scene = obs_scene_from_source(sceneSrc);
		if (!scene)
			continue;

		obs_sceneitem_t *existing = obs_scene_find_source(scene, m_sceneName.c_str());
		if (existing) {
			obs_sceneitem_set_order(existing, OBS_ORDER_MOVE_TOP);
			continue;
		}

		obs_sceneitem_t *item = obs_scene_add(scene, boardSrc);
		if (item)
			obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
	}

	obs_frontend_source_list_free(&list);
	obs_source_release(boardSrc);
	blog(LOG_INFO, "[soundboard] Added '%s' to all scenes", m_sceneName.c_str());
}

// ── Hotkeys ───────────────────────────────────────────────────────────────────

void SoundboardManager::registerHotkeys()
{
	obs_scene_t *scene = boardScene();
	if (!scene)
		return;

	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) -> bool {
			auto *mgr = static_cast<SoundboardManager *>(param);
			obs_source_t *src = obs_sceneitem_get_source(item);
			if (!src)
				return true;
			const char *name = obs_source_get_name(src);
			if (!name || !*name)
				return true;

			std::string sname = name;
			for (const auto &hk : mgr->m_hotkeys)
				if (hk.sourceName == sname)
					return true;

			std::string id = "soundboard_play_" + sname;
			std::string desc = "Soundboard: Play \"" + sname + "\"";

			struct Ctx {
				SoundboardManager *mgr;
				std::string name;
			};
			auto *ctx = new Ctx{mgr, sname};

			obs_hotkey_id hkId = obs_hotkey_register_frontend(id.c_str(), desc.c_str(), cbHotkeyPlay, ctx);

			mgr->m_hotkeys.push_back({hkId, sname});
			return true;
		},
		this);
}

void SoundboardManager::unregisterAllHotkeys()
{
	for (const auto &hk : m_hotkeys)
		obs_hotkey_unregister(hk.id);
	m_hotkeys.clear();
}

void SoundboardManager::cbHotkeyPlay(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	struct Ctx {
		SoundboardManager *mgr;
		std::string name;
	};
	auto *ctx = static_cast<Ctx *>(data);
	ctx->mgr->play(ctx->name);
}

// ── Scene signals ─────────────────────────────────────────────────────────────

void SoundboardManager::connectSceneSignals(obs_source_t *sceneSource)
{
	signal_handler_t *sh = obs_source_get_signal_handler(sceneSource);
	signal_handler_connect(sh, "item_add", cbItemAdd, this);
	signal_handler_connect(sh, "item_remove", cbItemRemove, this);
}

void SoundboardManager::disconnectSceneSignals(obs_source_t *sceneSource)
{
	signal_handler_t *sh = obs_source_get_signal_handler(sceneSource);
	signal_handler_disconnect(sh, "item_add", cbItemAdd, this);
	signal_handler_disconnect(sh, "item_remove", cbItemRemove, this);
}

void SoundboardManager::cbItemAdd(void *data, calldata_t *)
{
	auto *mgr = static_cast<SoundboardManager *>(data);

	mgr->unregisterAllHotkeys();
	mgr->registerHotkeys();

	if (mgr->m_refreshCb)
		mgr->m_refreshCb();
}

void SoundboardManager::cbItemRemove(void *data, calldata_t *)
{
	auto *mgr = static_cast<SoundboardManager *>(data);

	mgr->unregisterAllHotkeys();
	mgr->registerHotkeys();

	if (mgr->m_refreshCb)
		mgr->m_refreshCb();
}

// ── Persistence ───────────────────────────────────────────────────────────────

void SoundboardManager::loadSettings()
{
	char *path = obs_module_config_path("settings.json");
	obs_data_t *root = obs_data_create_from_json_file(path);
	bfree(path);

	if (root) {
		const char *scene = obs_data_get_string(root, "board_scene");
		if (scene && *scene)
			m_sceneName = scene;

		m_httpPort = (int)obs_data_get_int(root, "http_port");
		if (m_httpPort <= 0 || m_httpPort > 65535)
			m_httpPort = 4489;

		const char *ds = obs_data_get_string(root, "dock_state");
		if (ds)
			m_dockState = ds;

		obs_data_array_t *clips = obs_data_get_array(root, "clip_config");
		if (clips) {
			size_t count = obs_data_array_count(clips);
			for (size_t i = 0; i < count; i++) {
				obs_data_t *entry = obs_data_array_item(clips, i);
				const char *name = obs_data_get_string(entry, "source");
				if (name && *name) {
					SoundboardClipConfig cfg;
					cfg.startSec = obs_data_get_double(entry, "start_sec");
					cfg.durationSec = obs_data_get_double(entry, "duration_sec");

					obs_data_array_t *devices = obs_data_get_array(entry, "extra_output_devices");
					if (devices) {
						size_t deviceCount = obs_data_array_count(devices);
						for (size_t d = 0; d < deviceCount; d++) {
							obs_data_t *deviceEntry = obs_data_array_item(devices, d);
							const char *deviceName =
								obs_data_get_string(deviceEntry, "name");
							if (deviceName && *deviceName)
								cfg.extraOutputDevices.push_back(deviceName);
							obs_data_release(deviceEntry);
						}
						obs_data_array_release(devices);
					}

					m_clipConfig[name] = cfg;
				}
				obs_data_release(entry);
			}
			obs_data_array_release(clips);
		}

		obs_data_release(root);
	}

	obs_source_t *src = obs_get_source_by_name(m_sceneName.c_str());
	if (src) {
		connectSceneSignals(src);
		obs_source_release(src);
	}
	registerHotkeys();
}

void SoundboardManager::saveSettings()
{
	char *dir = obs_module_config_path("");
	os_mkdirs(dir);
	bfree(dir);

	obs_data_t *root = obs_data_create();
	obs_data_set_string(root, "board_scene", m_sceneName.c_str());
	obs_data_set_int(root, "http_port", m_httpPort);
	obs_data_set_string(root, "dock_state", m_dockState.c_str());

	obs_data_array_t *clips = obs_data_array_create();
	for (const auto &[name, cfg] : m_clipConfig) {
		obs_data_t *entry = obs_data_create();
		obs_data_set_string(entry, "source", name.c_str());
		obs_data_set_double(entry, "start_sec", cfg.startSec);
		obs_data_set_double(entry, "duration_sec", cfg.durationSec);

		obs_data_array_t *devices = obs_data_array_create();
		for (const auto &deviceName : cfg.extraOutputDevices) {
			obs_data_t *deviceEntry = obs_data_create();
			obs_data_set_string(deviceEntry, "name", deviceName.c_str());
			obs_data_array_push_back(devices, deviceEntry);
			obs_data_release(deviceEntry);
		}
		obs_data_set_array(entry, "extra_output_devices", devices);
		obs_data_array_release(devices);

		obs_data_array_push_back(clips, entry);
		obs_data_release(entry);
	}
	obs_data_set_array(root, "clip_config", clips);
	obs_data_array_release(clips);

	char *path = obs_module_config_path("settings.json");
	obs_data_save_json_safe(root, path, "tmp", "bak");
	bfree(path);
	obs_data_release(root);
}
