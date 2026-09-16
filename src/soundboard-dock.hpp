#pragma once

#include <QWidget>
#include <QGridLayout>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QTimer>
#include <QString>
#include <QHash>
#include <string>

class SoundboardDock : public QWidget {
	Q_OBJECT

public:
	explicit SoundboardDock(QWidget *parent = nullptr);

private slots:
	void refresh();
	void pollPlayingState();
	void onPadClicked(const QString &sourceName);
	void onPadSettingsClicked(const QString &sourceName);
	void onStopAllClicked();
	void onSettingsClicked();
	void onAddSoundClicked();

private:
	void buildUI();

	static void stylePad(QPushButton *btn, bool playing, bool compact);

	QLabel *m_sceneLabel = nullptr;
	QScrollArea *m_scroll = nullptr;
	QWidget *m_padContainer = nullptr;
	QTimer *m_pollTimer = nullptr;
	QHash<QString, QPushButton *> m_pads;
	QHash<QString, QProgressBar *> m_bars;
	bool m_compact = false; // mirrors SoundboardManager::listMode() as of the last refresh()
};
