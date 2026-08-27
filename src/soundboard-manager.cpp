#include "soundboard-manager.hpp"

#include <obs-frontend-api.h>
#include <util/platform.h>
#include <callback/signal.h>

#include <QCoreApplication>
#include <QMetaObject>
#include <QTimer>

#include <cstring>

// ── Singleton ─────────────────────────────────────────────────────────────────

SoundboardManager &SoundboardManager::instance()
{
    static SoundboardManager inst;
    return inst;
}

SoundboardManager::SoundboardManager() {}

SoundboardManager::~SoundboardManager()
{
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
    if (!src) return nullptr;
    obs_scene_t *scene = obs_scene_from_source(src);
    obs_source_release(src);
    return scene;
}

obs_sceneitem_t *SoundboardManager::findItem(const std::string &sourceName) const
{
    obs_scene_t *scene = boardScene();
    if (!scene) return nullptr;
    return obs_scene_find_source(scene, sourceName.c_str());
}

// ── Scene name change ─────────────────────────────────────────────────────────

void SoundboardManager::setSceneName(const std::string &name)
{
    if (name == m_sceneName) return;

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

    if (m_refreshCb) m_refreshCb();
}

// ── Clip enumeration ─────────────────────────────────────────────────────────

std::vector<SoundboardManager::ClipInfo> SoundboardManager::currentClips() const
{
    std::vector<ClipInfo> result;
    obs_scene_t *scene = boardScene();
    if (!scene) return result;

    obs_scene_enum_items(scene,
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
    if (!item) return;
    obs_source_t *src = obs_sceneitem_get_source(item);
    if (!src) return;

    obs_source_media_restart(src);

    auto it = m_clipConfig.find(sourceName);
    if (it == m_clipConfig.end()) return;
    const SoundboardClipConfig &cfg = it->second;

    if (cfg.startSec > 0.0)
        obs_source_media_set_time(src, static_cast<int64_t>(cfg.startSec * 1000));

    if (cfg.durationSec > 0.0) {
        uint64_t gen = ++m_playGen[sourceName];
        std::string name = sourceName;
        int ms = static_cast<int>(cfg.durationSec * 1000);
        // Hop onto the Qt main thread first: play() can be called from the
        // OBS hotkey-dispatch thread, which doesn't pump a Qt event loop, so
        // a QTimer started there would never fire.
        QMetaObject::invokeMethod(qApp, [this, name, gen, ms]() {
            QTimer::singleShot(ms, [this, name, gen]() {
                if (m_playGen[name] == gen) stopOne(name);
            });
        }, Qt::QueuedConnection);
    }
}

void SoundboardManager::stopOne(const std::string &sourceName)
{
    obs_sceneitem_t *item = findItem(sourceName);
    if (!item) return;
    obs_source_t *src = obs_sceneitem_get_source(item);
    if (src) obs_source_media_stop(src);
}

void SoundboardManager::stopAll()
{
    for (const auto &clip : currentClips())
        stopOne(clip.sourceName);
}

bool SoundboardManager::isPlaying(const std::string &sourceName) const
{
    obs_sceneitem_t *item = findItem(sourceName);
    if (!item) return false;
    obs_source_t *src = obs_sceneitem_get_source(item);
    if (!src) return false;
    return obs_source_media_get_state(src) == OBS_MEDIA_STATE_PLAYING;
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
    applyMonitoring(sourceName);
}

// Per-source audio monitoring: in addition to always going through the main
// program mix, a monitored clip is also sent to OBS's single, globally
// configured Monitoring Device (Settings -> Audio -> Advanced) — e.g.
// headphones or an external monitor's audio output. OBS doesn't support
// routing different sources to different monitor devices; there's only one
// shared monitor device for the whole app.
void SoundboardManager::applyMonitoring(const std::string &sourceName)
{
    obs_sceneitem_t *item = findItem(sourceName);
    if (!item) return;
    obs_source_t *src = obs_sceneitem_get_source(item);
    if (!src) return;

    auto it = m_clipConfig.find(sourceName);
    bool monitor = it != m_clipConfig.end() && it->second.monitor;
    obs_source_set_monitoring_type(src,
        monitor ? OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT : OBS_MONITORING_TYPE_NONE);
}

// ── Setup helper ──────────────────────────────────────────────────────────────

void SoundboardManager::addToAllScenes()
{
    obs_source_t *boardSrc = obs_get_source_by_name(m_sceneName.c_str());
    if (!boardSrc) {
        obs_scene_t *newScene = obs_scene_create(m_sceneName.c_str());
        if (!newScene) return;
        boardSrc = obs_source_get_ref(obs_scene_get_source(newScene));
    }

    struct obs_frontend_source_list list = {};
    obs_frontend_get_scenes(&list);

    for (size_t i = 0; i < list.sources.num; i++) {
        obs_source_t *sceneSrc  = list.sources.array[i];
        const char   *sceneName = obs_source_get_name(sceneSrc);
        if (sceneName && strcmp(sceneName, m_sceneName.c_str()) == 0) continue;

        obs_scene_t *scene = obs_scene_from_source(sceneSrc);
        if (!scene) continue;

        obs_sceneitem_t *existing = obs_scene_find_source(scene, m_sceneName.c_str());
        if (existing) {
            obs_sceneitem_set_order(existing, OBS_ORDER_MOVE_TOP);
            continue;
        }

        obs_sceneitem_t *item = obs_scene_add(scene, boardSrc);
        if (item) obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
    }

    obs_frontend_source_list_free(&list);
    obs_source_release(boardSrc);
    blog(LOG_INFO, "[soundboard] Added '%s' to all scenes", m_sceneName.c_str());
}

// ── Hotkeys ───────────────────────────────────────────────────────────────────

void SoundboardManager::registerHotkeys()
{
    obs_scene_t *scene = boardScene();
    if (!scene) return;

    obs_scene_enum_items(scene,
        [](obs_scene_t *, obs_sceneitem_t *item, void *param) -> bool {
            auto *mgr = static_cast<SoundboardManager *>(param);
            obs_source_t *src = obs_sceneitem_get_source(item);
            if (!src) return true;
            const char *name = obs_source_get_name(src);
            if (!name || !*name) return true;

            std::string sname = name;
            for (const auto &hk : mgr->m_hotkeys)
                if (hk.sourceName == sname) return true;

            std::string id   = "soundboard_play_" + sname;
            std::string desc = "Soundboard: Play \"" + sname + "\"";

            struct Ctx { SoundboardManager *mgr; std::string name; };
            auto *ctx = new Ctx{mgr, sname};

            obs_hotkey_id hkId = obs_hotkey_register_frontend(
                id.c_str(), desc.c_str(), cbHotkeyPlay, ctx);

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
    if (!pressed) return;
    struct Ctx { SoundboardManager *mgr; std::string name; };
    auto *ctx = static_cast<Ctx *>(data);
    ctx->mgr->play(ctx->name);
}

// ── Scene signals ─────────────────────────────────────────────────────────────

void SoundboardManager::connectSceneSignals(obs_source_t *sceneSource)
{
    signal_handler_t *sh = obs_source_get_signal_handler(sceneSource);
    signal_handler_connect(sh, "item_add",    cbItemAdd,    this);
    signal_handler_connect(sh, "item_remove", cbItemRemove, this);
}

void SoundboardManager::disconnectSceneSignals(obs_source_t *sceneSource)
{
    signal_handler_t *sh = obs_source_get_signal_handler(sceneSource);
    signal_handler_disconnect(sh, "item_add",    cbItemAdd,    this);
    signal_handler_disconnect(sh, "item_remove", cbItemRemove, this);
}

void SoundboardManager::cbItemAdd(void *data, calldata_t *cd)
{
    auto *mgr = static_cast<SoundboardManager *>(data);

    obs_sceneitem_t *item = nullptr;
    calldata_get_ptr(cd, "item", &item);
    if (item) {
        obs_source_t *src = obs_sceneitem_get_source(item);
        if (src) {
            const char *name = obs_source_get_name(src);
            if (name) mgr->applyMonitoring(name);
        }
    }

    mgr->unregisterAllHotkeys();
    mgr->registerHotkeys();

    if (mgr->m_refreshCb) mgr->m_refreshCb();
}

void SoundboardManager::cbItemRemove(void *data, calldata_t *)
{
    auto *mgr = static_cast<SoundboardManager *>(data);

    mgr->unregisterAllHotkeys();
    mgr->registerHotkeys();

    if (mgr->m_refreshCb) mgr->m_refreshCb();
}

// ── Persistence ───────────────────────────────────────────────────────────────

void SoundboardManager::loadSettings()
{
    char *path = obs_module_config_path("settings.json");
    obs_data_t *root = obs_data_create_from_json_file(path);
    bfree(path);

    if (root) {
        const char *scene = obs_data_get_string(root, "board_scene");
        if (scene && *scene) m_sceneName = scene;

        m_httpPort = (int)obs_data_get_int(root, "http_port");
        if (m_httpPort <= 0 || m_httpPort > 65535) m_httpPort = 4489;

        const char *ds = obs_data_get_string(root, "dock_state");
        if (ds) m_dockState = ds;

        obs_data_array_t *clips = obs_data_get_array(root, "clip_config");
        if (clips) {
            size_t count = obs_data_array_count(clips);
            for (size_t i = 0; i < count; i++) {
                obs_data_t *entry = obs_data_array_item(clips, i);
                const char *name = obs_data_get_string(entry, "source");
                if (name && *name) {
                    SoundboardClipConfig cfg;
                    cfg.startSec    = obs_data_get_double(entry, "start_sec");
                    cfg.durationSec = obs_data_get_double(entry, "duration_sec");
                    cfg.monitor     = obs_data_get_bool(entry, "monitor");
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

    // Apply saved monitoring settings to any items already in the scene.
    for (const auto &clip : currentClips())
        applyMonitoring(clip.sourceName);
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
        obs_data_set_string(entry, "source",      name.c_str());
        obs_data_set_double(entry, "start_sec",   cfg.startSec);
        obs_data_set_double(entry, "duration_sec", cfg.durationSec);
        obs_data_set_bool(entry,   "monitor",     cfg.monitor);
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
