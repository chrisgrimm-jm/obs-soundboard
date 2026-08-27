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

// ── Styles ────────────────────────────────────────────────────────────────────

static const char *kPlaying =
    "QPushButton {"
    "  background: #27ae60; color: #fff;"
    "  border: 2px solid #1e8449; border-radius: 6px;"
    "  font-weight: bold; font-size: 12px; padding: 14px 8px;"
    "}"
    "QPushButton:hover { background: #2ecc71; }";

static const char *kIdle =
    "QPushButton {"
    "  background: #2c2c2c; color: #ddd;"
    "  border: 2px solid #444; border-radius: 6px;"
    "  font-weight: bold; font-size: 12px; padding: 14px 8px;"
    "}"
    "QPushButton:hover { background: #3a3a3a; border-color: #666; }";

static const char *kStopAllBtn =
    "QPushButton {"
    "  background: #a93226; color: #fff; border: none;"
    "  border-radius: 3px; padding: 4px 12px; font-weight: bold; font-size: 11px;"
    "}"
    "QPushButton:hover { background: #c0392b; }";

static const char *kSettingsBtn =
    "QPushButton {"
    "  background: #1a1a1a; color: #777;"
    "  border: 1px solid #333; border-radius: 3px;"
    "  font-size: 11px; padding: 3px 10px;"
    "}"
    "QPushButton:hover { color: #bbb; border-color: #555; }";

static const char *kPadSettingsBtn =
    "QPushButton {"
    "  background: #1a1a1a; color: #666;"
    "  border: 1px solid #333; border-radius: 3px;"
    "  font-size: 10px; padding: 1px;"
    "}"
    "QPushButton:hover { color: #aaa; border-color: #555; }";

// ── Constructor ───────────────────────────────────────────────────────────────

SoundboardDock::SoundboardDock(QWidget *parent) : QWidget(parent)
{
    buildUI();

    auto &mgr = SoundboardManager::instance();
    mgr.setRefreshCallback([this]() {
        QMetaObject::invokeMethod(this, "refresh", Qt::QueuedConnection);
    });

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

void SoundboardDock::stylePad(QPushButton *btn, bool playing)
{
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

    auto clips = mgr.currentClips();
    const int columns = 3;
    int row = 0, col = 0;
    for (const auto &clip : clips) {
        QString sname = QString::fromStdString(clip.sourceName);

        auto *tile = new QWidget();
        auto *tileLayout = new QVBoxLayout(tile);
        tileLayout->setContentsMargins(0, 0, 0, 0);
        tileLayout->setSpacing(2);

        auto *pad = new QPushButton(sname);
        pad->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        pad->setMinimumHeight(56);
        stylePad(pad, false);
        connect(pad, &QPushButton::clicked, this, [this, sname]() {
            onPadClicked(sname);
        });
        tileLayout->addWidget(pad);

        auto *settingsBtn = new QPushButton("⚙");
        settingsBtn->setStyleSheet(kPadSettingsBtn);
        settingsBtn->setFixedHeight(16);
        settingsBtn->setToolTip("Trim / monitoring settings for this clip");
        connect(settingsBtn, &QPushButton::clicked, this, [this, sname]() {
            onPadSettingsClicked(sname);
        });
        tileLayout->addWidget(settingsBtn);

        grid->addWidget(tile, row, col);
        m_pads[sname] = pad;

        if (++col >= columns) { col = 0; row++; }
    }

    if (clips.empty()) {
        auto *hint = new QLabel(
            "Open Settings to choose your\nSoundboard scene, then add\n"
            "sound clips to it in OBS\n(Add Source → Media Source).");
        hint->setAlignment(Qt::AlignCenter);
        hint->setStyleSheet("color: #555; font-size: 11px;");
        grid->addWidget(hint, 0, 0);
    }

    m_scroll->setWidget(m_padContainer);
}

void SoundboardDock::pollPlayingState()
{
    auto &mgr = SoundboardManager::instance();
    for (auto it = m_pads.constBegin(); it != m_pads.constEnd(); ++it)
        stylePad(it.value(), mgr.isPlaying(it.key().toStdString()));
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
