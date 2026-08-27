#pragma once

#include <QDialog>
#include <QComboBox>
#include <QSpinBox>

class SoundboardSettings : public QDialog {
    Q_OBJECT

public:
    explicit SoundboardSettings(QWidget *parent = nullptr);

private slots:
    void onAddToAllScenes();
    void onAccept();

private:
    void buildUI();
    void populateSceneCombo();

    QComboBox *m_sceneCombo   = nullptr;
    QSpinBox  *m_httpPortSpin = nullptr;
};
