#pragma once

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QListWidget>
#include <QRadioButton>
#include <QString>

class SoundboardItemSettings : public QDialog {
	Q_OBJECT

public:
	explicit SoundboardItemSettings(const QString &sourceName, QWidget *parent = nullptr);

private slots:
	void onTest();
	void onAccept();

private:
	void buildUI();
	void applyPending();

	QString m_sourceName;

	QDoubleSpinBox *m_startSpin = nullptr;
	QDoubleSpinBox *m_durationSpin = nullptr;
	QCheckBox *m_loopCheck = nullptr;
	QButtonGroup *m_clickActionGroup = nullptr;
	QRadioButton *m_clickStopRadio = nullptr;
	QRadioButton *m_clickFadeRadio = nullptr;
	QRadioButton *m_clickRetriggerRadio = nullptr;
	QListWidget *m_deviceList = nullptr;
};
