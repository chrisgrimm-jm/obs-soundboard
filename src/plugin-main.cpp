#include "plugin-main.h"
#include "soundboard-manager.hpp"
#include "soundboard-dock.hpp"
#include "companion-server.hpp"

#include <QMainWindow>
#include <QTimer>

static SoundboardDock *s_dock = nullptr;

static void on_frontend_event(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		SoundboardManager::instance().loadSettings();

		s_dock = new SoundboardDock();
		obs_frontend_add_dock_by_id("SoundboardDock", "Soundboard", s_dock);

		// Restore dock position: defer one event-loop tick so OBS finishes
		// placing the dock before we override with our saved layout.
		const std::string saved = SoundboardManager::instance().dockState();
		if (!saved.empty()) {
			QTimer::singleShot(0, []() {
				auto *mainWin = static_cast<QMainWindow *>(obs_frontend_get_main_window());
				if (!mainWin)
					return;
				QByteArray state = QByteArray::fromBase64(
					QByteArray::fromStdString(SoundboardManager::instance().dockState()));
				mainWin->restoreState(state);
			});
		}

		g_companionServer = new CompanionServer();
		g_companionServer->start(static_cast<quint16>(SoundboardManager::instance().httpPort()));

	} else if (event == OBS_FRONTEND_EVENT_EXIT) {
		auto *mainWin = static_cast<QMainWindow *>(obs_frontend_get_main_window());
		if (mainWin)
			SoundboardManager::instance().setDockState(mainWin->saveState().toBase64().toStdString());

		SoundboardManager::instance().saveSettings();

		if (g_companionServer) {
			g_companionServer->stop();
			delete g_companionServer;
			g_companionServer = nullptr;
		}
	}
}

bool obs_module_load()
{
	blog(LOG_INFO, "[soundboard] Loading v%s", PLUGIN_VERSION);
	obs_frontend_add_event_callback(on_frontend_event, nullptr);
	return true;
}

void obs_module_unload()
{
	blog(LOG_INFO, "[soundboard] Unloading");
	auto *mainWin = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (mainWin)
		SoundboardManager::instance().setDockState(mainWin->saveState().toBase64().toStdString());
	SoundboardManager::instance().saveSettings();
}

const char *obs_module_description()
{
	return "Soundboard dock — trigger sound-clip sources from one nested scene";
}
