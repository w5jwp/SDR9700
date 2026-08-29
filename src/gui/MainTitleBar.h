// cppcheck-suppress-file unusedStructMember
#pragma once

#include <QWidget>

#include <functional>

class QMenu;
class QSlider;
class QPushButton;
class QLabel;
class QHBoxLayout;
class QMouseEvent;
class QPaintEvent;
class QTimer;

class MainTitleBar : public QWidget
{
    Q_OBJECT

  public:
    explicit MainTitleBar(QWidget* parent = nullptr);

    void addMenu(const QString& label, QMenu* menu);
    void addAction(const QString& label, QObject* context, std::function<void()> callback);
    void setTitle(const QString& title);
    void setVolume(int value);
    void setMuted(bool muted);
    void setVolumeEnabled(bool enabled);
    void setLocked(bool locked);
    void setTxDuration(const QString& duration, bool transmitting);
    void setTxDurationActive(bool transmitting);
    void setLanMod(int value);
    void setLanModEnabled(bool enabled);
    void pulseRadioHeartbeat();
    void clearRadioHeartbeat();

  signals:
    void volumeChanged(int value);
    void lanModChanged(int value);
    void muteToggled();
    void lockToggled();
    void txDurationResetRequested();
    void minimizeRequested();
    void closeRequested();

  protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

  private:
    QHBoxLayout* m_menuLayout{nullptr};
    QLabel* m_titleLabel{nullptr};
    QLabel* m_heartbeatIndicator{nullptr};
    QTimer* m_heartbeatFadeTimer{nullptr};
    QSlider* m_lanModSlider{nullptr};
    QLabel* m_lanModLabel{nullptr};
    QPushButton* m_txDurationButton{nullptr};
    QPushButton* m_muteBtn{nullptr};
    QPushButton* m_lockBtn{nullptr};
    QSlider* m_volumeSlider{nullptr};
    QLabel* m_volumeLabel{nullptr};
    QPushButton* m_minimizeBtn{nullptr};
    QPushButton* m_closeBtn{nullptr};
};
