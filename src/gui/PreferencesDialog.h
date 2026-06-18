// cppcheck-suppress-file unusedStructMember
#pragma once

#include <QDialog>

class QWidget;
class QComboBox;
class QCheckBox;
class QLabel;
class QSlider;

class PreferencesDialog : public QDialog
{
    Q_OBJECT

  public:
    enum class Page
    {
        RadioSetup,
        Audio,
        Application,
    };

    explicit PreferencesDialog(QWidget* parent = nullptr);
    explicit PreferencesDialog(Page page, QWidget* parent = nullptr);

  private slots:
    void saveAudioSettings();

  private:
    QWidget* buildRadioConnectionsTab();
    QWidget* buildAudioTab();
    QWidget* buildSoftwareTab();

    QSlider* m_lanModLevelSlider{nullptr};
    QLabel* m_lanModLevelValue{nullptr};
    QCheckBox* m_invertPanadapterMouseWheelCheck{nullptr};

    QComboBox* m_inputCombo{nullptr};
    QComboBox* m_outputCombo{nullptr};
    QComboBox* m_outputChannelsCombo{nullptr};
};
