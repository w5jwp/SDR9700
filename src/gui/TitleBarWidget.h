// cppcheck-suppress-file unusedStructMember
#pragma once

#include <QWidget>

class QMenu;
class QSlider;
class QPushButton;
class QLabel;
class QHBoxLayout;

class TitleBarWidget : public QWidget
{
    Q_OBJECT

  public:
    explicit TitleBarWidget(QWidget* parent = nullptr);

    void addMenu(const QString& label, QMenu* menu);
    void setTitle(const QString& title);
    void setVolume(int value);
    void setMuted(bool muted);
    void setVolumeEnabled(bool enabled);
    void setLocked(bool locked);

  signals:
    void volumeChanged(int value);
    void muteToggled();
    void lockToggled();
    void minimizeRequested();
    void closeRequested();

  protected:
    void mousePressEvent(QMouseEvent* event) override;

  private:
    QHBoxLayout* m_menuLayout{nullptr};
    QLabel* m_titleLabel{nullptr};
    QPushButton* m_muteBtn{nullptr};
    QPushButton* m_lockBtn{nullptr};
    QSlider* m_volumeSlider{nullptr};
    QLabel* m_volumeLabel{nullptr};
    QPushButton* m_minimizeBtn{nullptr};
    QPushButton* m_closeBtn{nullptr};
};
