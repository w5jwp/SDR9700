#pragma once

#include <QObject>

class MainWindow;
class QVBoxLayout;

class ControlPanelController : public QObject
{
    Q_OBJECT

  public:
    explicit ControlPanelController(MainWindow* window);

    void buildControlPanel(QVBoxLayout* vbox);

  private:
    MainWindow* m_window{nullptr};
};
