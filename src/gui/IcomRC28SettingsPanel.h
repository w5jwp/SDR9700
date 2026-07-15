#pragma once

#ifdef HAVE_HIDAPI

#include <QWidget>

class IcomRC28Manager;
class QLabel;
class QCheckBox;
class QComboBox;
class QSlider;

class IcomRC28SettingsPanel : public QWidget
{
    Q_OBJECT

  public:
    explicit IcomRC28SettingsPanel(IcomRC28Manager* manager, QWidget* parent = nullptr);

  signals:
    void encoderSettingsChanged(const QString& field, const QString& value);

  private:
    void buildUi();
    void refreshDeviceInfo();
    void appendLog(const QString& text);
    void loadSettings();
    void saveActionField(const QString& field, const QString& value);
    void savePttMode(const QString& mode);
    void saveSensitivity(int value);
    void saveAutoSnap(bool on);

    IcomRC28Manager* m_manager{nullptr};
    QLabel* m_statusLabel{nullptr};
    QLabel* m_devicePathLabel{nullptr};
    QLabel* m_serialLabel{nullptr};
    QComboBox* m_f1PressCombo{nullptr};
    QComboBox* m_f1HoldCombo{nullptr};
    QComboBox* m_f2PressCombo{nullptr};
    QComboBox* m_f2HoldCombo{nullptr};
    QComboBox* m_pttModeCombo{nullptr};
    QSlider* m_sensitivitySlider{nullptr};
    QLabel* m_sensitivityValueLabel{nullptr};
    QCheckBox* m_autoSnapCheck{nullptr};
};

#endif
