#include "soundboard-item-settings.hpp"
#include "soundboard-manager.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>

SoundboardItemSettings::SoundboardItemSettings(const QString &sourceName, QWidget *parent)
    : QDialog(parent), m_sourceName(sourceName)
{
    setWindowTitle("Clip Settings — " + sourceName);
    setMinimumWidth(380);
    buildUI();
}

void SoundboardItemSettings::buildUI()
{
    auto *root = new QVBoxLayout(this);
    root->setSpacing(12);

    auto cfg = SoundboardManager::instance().clipConfig(m_sourceName.toStdString());

    auto *trimGroup = new QGroupBox("Trim");
    auto *trimLayout = new QVBoxLayout(trimGroup);

    auto *trimHint = new QLabel(
        "Where playback starts and how long it plays before auto-stopping. "
        "Duration 0 means play to the clip's natural end.");
    trimHint->setWordWrap(true);
    trimHint->setStyleSheet("color: #999; font-size: 11px;");
    trimLayout->addWidget(trimHint);

    auto *form = new QFormLayout();
    m_startSpin = new QDoubleSpinBox();
    m_startSpin->setRange(0.0, 3600.0);
    m_startSpin->setDecimals(2);
    m_startSpin->setSuffix(" s");
    m_startSpin->setValue(cfg.startSec);
    form->addRow("Start (in point):", m_startSpin);

    m_durationSpin = new QDoubleSpinBox();
    m_durationSpin->setRange(0.0, 3600.0);
    m_durationSpin->setDecimals(2);
    m_durationSpin->setSuffix(" s");
    m_durationSpin->setValue(cfg.durationSec);
    form->addRow("Duration (0 = to end):", m_durationSpin);
    trimLayout->addLayout(form);

    root->addWidget(trimGroup);

    auto *outGroup  = new QGroupBox("Outputs");
    auto *outLayout = new QVBoxLayout(outGroup);

    auto *outHint = new QLabel(
        "Every clip always plays through the main program mix. Check this to "
        "also send it to OBS's Monitoring Device (Settings -> Audio -> "
        "Advanced -> Monitoring Device — set that once to your headphones or "
        "external monitor's audio output; every monitored clip shares it).");
    outHint->setWordWrap(true);
    outHint->setStyleSheet("color: #999; font-size: 11px;");
    outLayout->addWidget(outHint);

    m_monitorCheck = new QCheckBox("Also send to Monitoring Device");
    m_monitorCheck->setChecked(cfg.monitor);
    outLayout->addWidget(m_monitorCheck);

    root->addWidget(outGroup);

    auto *btnRow = new QHBoxLayout();
    auto *testBtn = new QPushButton("Test");
    connect(testBtn, &QPushButton::clicked, this, &SoundboardItemSettings::onTest);
    btnRow->addWidget(testBtn);
    btnRow->addStretch();
    root->addLayout(btnRow);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, this, &SoundboardItemSettings::onAccept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(btns);
}

void SoundboardItemSettings::applyPending()
{
    SoundboardClipConfig cfg;
    cfg.startSec    = m_startSpin->value();
    cfg.durationSec = m_durationSpin->value();
    cfg.monitor     = m_monitorCheck->isChecked();
    SoundboardManager::instance().setClipConfig(m_sourceName.toStdString(), cfg);
}

void SoundboardItemSettings::onTest()
{
    applyPending();
    SoundboardManager::instance().play(m_sourceName.toStdString());
}

void SoundboardItemSettings::onAccept()
{
    applyPending();
    SoundboardManager::instance().saveSettings();
    accept();
}
