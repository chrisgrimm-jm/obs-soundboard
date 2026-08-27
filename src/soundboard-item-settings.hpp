#pragma once

#include <QDialog>
#include <QDoubleSpinBox>
#include <QListWidget>
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

    QDoubleSpinBox *m_startSpin    = nullptr;
    QDoubleSpinBox *m_durationSpin = nullptr;
    QListWidget    *m_deviceList   = nullptr;
};
