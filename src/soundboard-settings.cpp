#include "soundboard-settings.hpp"
#include "soundboard-manager.hpp"
#include "companion-server.hpp"

#include <obs-frontend-api.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QMessageBox>

// ── Constructor ───────────────────────────────────────────────────────────────

SoundboardSettings::SoundboardSettings(QWidget *parent) : QDialog(parent)
{
	setWindowTitle("Soundboard Settings");
	setMinimumWidth(480);
	buildUI();
}

// ── UI ────────────────────────────────────────────────────────────────────────

void SoundboardSettings::buildUI()
{
	auto *root = new QVBoxLayout(this);
	root->setSpacing(12);

	// ── Scene selector ────────────────────────────────────────────────────────
	auto *sceneGroup = new QGroupBox("Soundboard Scene");
	auto *sceneLayout = new QVBoxLayout(sceneGroup);

	auto *sceneHint = new QLabel("Choose the OBS scene that holds your sound clips. Add each clip in "
				     "OBS normally (Add Source → Media Source) — the dock will show "
				     "every source in that scene as a pad, automatically.");
	sceneHint->setWordWrap(true);
	sceneHint->setStyleSheet("color: #999; font-size: 11px;");
	sceneLayout->addWidget(sceneHint);

	auto *sceneForm = new QFormLayout();
	m_sceneCombo = new QComboBox();
	populateSceneCombo();
	sceneForm->addRow("Soundboard scene:", m_sceneCombo);
	sceneLayout->addLayout(sceneForm);

	root->addWidget(sceneGroup);

	// ── Setup ─────────────────────────────────────────────────────────────────
	auto *setupGroup = new QGroupBox("Scene Setup");
	auto *setupLayout = new QVBoxLayout(setupGroup);

	auto *setupHint = new QLabel("Click below to automatically nest your Soundboard scene at the top "
				     "of every other scene in the current collection, same as a "
				     "downstream keyer. You only need to do this once, or again when you "
				     "add new scenes.");
	setupHint->setWordWrap(true);
	setupHint->setStyleSheet("color: #999; font-size: 11px;");
	setupLayout->addWidget(setupHint);

	auto *btnRow = new QHBoxLayout();
	btnRow->addStretch();
	auto *addBtn = new QPushButton("Add Soundboard scene to all scenes");
	addBtn->setStyleSheet("QPushButton { background:#2980b9; color:#fff; border:none;"
			      "  border-radius:3px; padding:5px 14px; }"
			      "QPushButton:hover { background:#3498db; }");
	connect(addBtn, &QPushButton::clicked, this, &SoundboardSettings::onAddToAllScenes);
	btnRow->addWidget(addBtn);
	setupLayout->addLayout(btnRow);
	root->addWidget(setupGroup);

	// ── Companion HTTP ────────────────────────────────────────────────────────
	auto *httpGroup = new QGroupBox("Bitfocus Companion / HTTP API");
	auto *httpForm = new QFormLayout(httpGroup);

	m_httpPortSpin = new QSpinBox();
	m_httpPortSpin->setRange(1024, 65535);
	m_httpPortSpin->setValue(SoundboardManager::instance().httpPort());
	httpForm->addRow("HTTP port:", m_httpPortSpin);

	auto *apiHint = new QLabel("GET /api/status   GET /api/clips\n"
				   "POST /api/clip/:name/play   POST /api/stopall");
	apiHint->setStyleSheet("color: #555; font-size: 11px; font-family: monospace;");
	httpForm->addRow(apiHint);
	root->addWidget(httpGroup);

	// ── Buttons ───────────────────────────────────────────────────────────────
	auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(btns, &QDialogButtonBox::accepted, this, &SoundboardSettings::onAccept);
	connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
	root->addWidget(btns);
}

void SoundboardSettings::populateSceneCombo()
{
	m_sceneCombo->clear();

	QString current = QString::fromStdString(SoundboardManager::instance().sceneName());

	struct obs_frontend_source_list list = {};
	obs_frontend_get_scenes(&list);
	for (size_t i = 0; i < list.sources.num; i++) {
		const char *name = obs_source_get_name(list.sources.array[i]);
		if (name)
			m_sceneCombo->addItem(QString::fromUtf8(name));
	}
	obs_frontend_source_list_free(&list);

	int idx = m_sceneCombo->findText(current);
	if (idx < 0)
		idx = m_sceneCombo->count() ? 0 : -1;
	if (idx >= 0)
		m_sceneCombo->setCurrentIndex(idx);
	else
		m_sceneCombo->addItem(current);
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void SoundboardSettings::onAddToAllScenes()
{
	SoundboardManager::instance().setSceneName(m_sceneCombo->currentText().toStdString());
	SoundboardManager::instance().addToAllScenes();

	QMessageBox::information(this, "Soundboard Setup",
				 "Done! Your Soundboard scene is now nested at the top of every "
				 "scene.\n\nTip: bind hotkeys per clip in OBS Settings → Hotkeys.");
}

void SoundboardSettings::onAccept()
{
	auto &mgr = SoundboardManager::instance();
	mgr.setSceneName(m_sceneCombo->currentText().toStdString());

	int newPort = m_httpPortSpin->value();
	if (newPort != mgr.httpPort()) {
		mgr.setHttpPort(newPort);
		extern CompanionServer *g_companionServer;
		if (g_companionServer) {
			g_companionServer->stop();
			g_companionServer->start(static_cast<quint16>(newPort));
		}
	}

	accept();
}
