#include "soundboard-dock.hpp"
#include "soundboard-manager.hpp"
#include "soundboard-settings.hpp"
#include "soundboard-item-settings.hpp"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QMetaObject>
#include <QSizePolicy>
#include <QFrame>
#include <QFileDialog>
#include <QStandardPaths>
#include <QMessageBox>

// ── Styles ────────────────────────────────────────────────────────────────────

static const char *kPlaying = "QPushButton {"
			      "  background: #27ae60; color: #fff;"
			      "  border: 2px solid #1e8449; border-radius: 6px;"
			      "  font-weight: bold; font-size: 12px; padding: 14px 8px;"
			      "}"
			      "QPushButton:hover { background: #2ecc71; }";

static const char *kIdle = "QPushButton {"
			   "  background: #2c2c2c; color: #ddd;"
			   "  border: 2px solid #444; border-radius: 6px;"
			   "  font-weight: bold; font-size: 12px; padding: 14px 8px;"
			   "}"
			   "QPushButton:hover { background: #3a3a3a; border-color: #666; }";

// List mode: compact, left-aligned, single-line rows instead of big tiles.
static const char *kPlayingCompact = "QPushButton {"
				     "  background: #27ae60; color: #fff; text-align: left;"
				     "  border: 1px solid #1e8449; border-radius: 4px;"
				     "  font-weight: bold; font-size: 12px; padding: 4px 10px;"
				     "}"
				     "QPushButton:hover { background: #2ecc71; }";

static const char *kIdleCompact = "QPushButton {"
				  "  background: #2c2c2c; color: #ddd; text-align: left;"
				  "  border: 1px solid #444; border-radius: 4px;"
				  "  font-weight: bold; font-size: 12px; padding: 4px 10px;"
				  "}"
				  "QPushButton:hover { background: #3a3a3a; border-color: #666; }";

static const char *kStopAllBtn = "QPushButton {"
				 "  background: #a93226; color: #fff; border: none;"
				 "  border-radius: 3px; padding: 4px 12px; font-weight: bold; font-size: 11px;"
				 "}"
				 "QPushButton:hover { background: #c0392b; }";

static const char *kSettingsBtn = "QPushButton {"
				  "  background: #1a1a1a; color: #777;"
				  "  border: 1px solid #333; border-radius: 3px;"
				  "  font-size: 11px; padding: 3px 10px;"
				  "}"
				  "QPushButton:hover { color: #bbb; border-color: #555; }";

// Countdown strip under each pad: full width at the start of a clip,
// shrinking to nothing as it plays out.
static const char *kCountdownBar = "QProgressBar {"
				   "  background: #1a1a1a; border: none; border-radius: 2px;"
				   "}"
				   "QProgressBar::chunk { background: #f39c12; border-radius: 2px; }";

// ── Constructor ───────────────────────────────────────────────────────────────

SoundboardDock::SoundboardDock(QWidget *parent) : QWidget(parent)
{
	buildUI();

	auto &mgr = SoundboardManager::instance();
	mgr.setRefreshCallback([this]() { QMetaObject::invokeMethod(this, "refresh", Qt::QueuedConnection); });

	// Playback state (playing/stopped) is owned by OBS's media pipeline, not
	// by us, so we poll rather than push — this also reflects clips started
	// via hotkey or the companion HTTP endpoint, not just dock clicks.
	m_pollTimer = new QTimer(this);
	connect(m_pollTimer, &QTimer::timeout, this, &SoundboardDock::pollPlayingState);
	m_pollTimer->start(300);
}

// ── UI build ──────────────────────────────────────────────────────────────────

void SoundboardDock::buildUI()
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(4, 4, 4, 4);
	root->setSpacing(4);

	// ── Top bar ───────────────────────────────────────────────────────────────
	auto *topBar = new QHBoxLayout();
	topBar->setContentsMargins(0, 0, 0, 0);

	m_sceneLabel = new QLabel();
	m_sceneLabel->setStyleSheet("color: #555; font-size: 10px; font-style: italic;");
	topBar->addWidget(m_sceneLabel);
	topBar->addStretch();

	auto *addSoundBtn = new QPushButton("+ Add Sound");
	addSoundBtn->setStyleSheet(kSettingsBtn);
	addSoundBtn->setFixedHeight(22);
	connect(addSoundBtn, &QPushButton::clicked, this, &SoundboardDock::onAddSoundClicked);
	topBar->addWidget(addSoundBtn);

	auto *stopAllBtn = new QPushButton("STOP ALL");
	stopAllBtn->setStyleSheet(kStopAllBtn);
	stopAllBtn->setFixedHeight(22);
	connect(stopAllBtn, &QPushButton::clicked, this, &SoundboardDock::onStopAllClicked);
	topBar->addWidget(stopAllBtn);

	auto *settingsBtn = new QPushButton("Settings");
	settingsBtn->setStyleSheet(kSettingsBtn);
	settingsBtn->setFixedHeight(22);
	connect(settingsBtn, &QPushButton::clicked, this, &SoundboardDock::onSettingsClicked);
	topBar->addWidget(settingsBtn);
	root->addLayout(topBar);

	// ── Scrollable pad grid ───────────────────────────────────────────────────
	m_scroll = new QScrollArea();
	m_scroll->setWidgetResizable(true);
	m_scroll->setFrameShape(QFrame::NoFrame);
	m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	m_padContainer = new QWidget();
	new QGridLayout(m_padContainer);
	m_scroll->setWidget(m_padContainer);
	root->addWidget(m_scroll);

	refresh();
}

void SoundboardDock::stylePad(QPushButton *btn, bool playing, bool compact)
{
	if (compact)
		btn->setStyleSheet(playing ? kPlayingCompact : kIdleCompact);
	else
		btn->setStyleSheet(playing ? kPlaying : kIdle);
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void SoundboardDock::refresh()
{
	auto &mgr = SoundboardManager::instance();
	m_sceneLabel->setText(QString::fromStdString(mgr.sceneName()));

	delete m_padContainer;
	m_padContainer = new QWidget();
	auto *grid = new QGridLayout(m_padContainer);
	grid->setSpacing(6);

	m_pads.clear();
	m_bars.clear();
	m_compact = mgr.listMode();

	auto clips = mgr.currentClips();
	const int columns = m_compact ? 1 : mgr.columns();
	int row = 0, col = 0;
	for (const auto &clip : clips) {
		QString sname = QString::fromStdString(clip.sourceName);

		auto *cell = new QWidget();
		auto *cellLayout = new QVBoxLayout(cell);
		cellLayout->setContentsMargins(0, 0, 0, 0);
		cellLayout->setSpacing(2);

		auto *pad = new QPushButton(sname);
		pad->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		pad->setMinimumHeight(m_compact ? 28 : 56);
		pad->setToolTip("Click to play/stop — right-click for trim & monitoring settings");
		stylePad(pad, false, m_compact);
		connect(pad, &QPushButton::clicked, this, [this, sname]() { onPadClicked(sname); });
		pad->setContextMenuPolicy(Qt::CustomContextMenu);
		connect(pad, &QPushButton::customContextMenuRequested, this,
			[this, sname]() { onPadSettingsClicked(sname); });
		cellLayout->addWidget(pad);

		// Countdown strip - full when a clip starts, empty when it ends.
		auto *bar = new QProgressBar();
		bar->setStyleSheet(kCountdownBar);
		bar->setRange(0, 1000);
		bar->setValue(0);
		bar->setTextVisible(false);
		bar->setFixedHeight(4);
		cellLayout->addWidget(bar);

		grid->addWidget(cell, row, col);
		m_pads[sname] = pad;
		m_bars[sname] = bar;

		if (++col >= columns) {
			col = 0;
			row++;
		}
	}

	if (clips.empty()) {
		auto *hint = new QLabel("Click + Add Sound above to add your\nfirst clip, or open Settings to\n"
					"choose a different Soundboard scene.");
		hint->setAlignment(Qt::AlignCenter);
		hint->setStyleSheet("color: #555; font-size: 11px;");
		grid->addWidget(hint, 0, 0);
	}

	m_scroll->setWidget(m_padContainer);
}

void SoundboardDock::pollPlayingState()
{
	auto &mgr = SoundboardManager::instance();
	for (auto it = m_pads.constBegin(); it != m_pads.constEnd(); ++it) {
		std::string name = it.key().toStdString();
		bool playing = mgr.isPlaying(name);
		stylePad(it.value(), playing, m_compact);

		auto *bar = m_bars.value(it.key());
		if (!bar)
			continue;
		double remaining = playing ? mgr.remainingFraction(name) : -1.0;
		bar->setValue(remaining >= 0.0 ? static_cast<int>(remaining * 1000) : 0);
	}
}

void SoundboardDock::onPadClicked(const QString &sourceName)
{
	auto &mgr = SoundboardManager::instance();
	std::string name = sourceName.toStdString();
	if (mgr.isPlaying(name))
		mgr.stopOne(name);
	else
		mgr.play(name);
}

void SoundboardDock::onPadSettingsClicked(const QString &sourceName)
{
	SoundboardItemSettings dlg(sourceName, this);
	dlg.exec();
}

void SoundboardDock::onStopAllClicked()
{
	SoundboardManager::instance().stopAll();
}

void SoundboardDock::onSettingsClicked()
{
	SoundboardSettings dlg(this);
	if (dlg.exec() == QDialog::Accepted) {
		SoundboardManager::instance().saveSettings();
		refresh();
	}
}

void SoundboardDock::onAddSoundClicked()
{
	QString startDir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
	QString path = QFileDialog::getOpenFileName(this, "Add Sound", startDir,
						    "Audio/Video files (*.mp3 *.wav *.aac *.ogg *.flac *.m4a *.mp4 "
						    "*.mov *.mkv);;All files (*)");
	if (path.isEmpty())
		return;

	std::string name = SoundboardManager::instance().addClipFromFile(path.toStdString());
	if (name.empty()) {
		QMessageBox::warning(this, "Add Sound", "Couldn't add that file as a clip.");
		return;
	}

	// The scene's item_add signal (SoundboardManager::cbItemAdd) already
	// queues a refresh, but do it directly too rather than rely on that
	// racing a signal callback - refresh() is idempotent either way.
	refresh();
}
