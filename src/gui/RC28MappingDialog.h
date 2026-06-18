#pragma once

#ifdef HAVE_HIDAPI

#include <QDialog>

class Rc28Manager;
class QLabel;
class QPlainTextEdit;
class QComboBox;

class RC28MappingDialog : public QDialog
{
    Q_OBJECT

  public:
    explicit RC28MappingDialog(Rc28Manager* manager, QWidget* parent = nullptr);

  private:
    void buildUi();
    void refreshDeviceInfo();
    void appendLog(const QString& text);
    void loadSettings();
    void saveActionField(const QString& field, const QString& value);
    void savePttMode(const QString& mode);

    Rc28Manager* m_manager{nullptr};
    QLabel* m_statusLabel{nullptr};
    QLabel* m_deviceNameLabel{nullptr};
    QLabel* m_devicePathLabel{nullptr};
    QLabel* m_serialLabel{nullptr};
    QLabel* m_releaseLabel{nullptr};
    QComboBox* m_f1PressCombo{nullptr};
    QComboBox* m_f1HoldCombo{nullptr};
    QComboBox* m_f2PressCombo{nullptr};
    QComboBox* m_f2HoldCombo{nullptr};
    QComboBox* m_pttModeCombo{nullptr};
    QPlainTextEdit* m_logView{nullptr};
};

#endif
