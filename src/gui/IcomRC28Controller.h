#pragma once

#include <QObject>
#include <QString>

class MainWindow;

class IcomRC28Controller : public QObject
{
  public:
    explicit IcomRC28Controller(MainWindow* window);

    void initialize();
    void close();
    void dispatchIcomRC28Action(const QString& action);
    void setIcomRC28Ptt(bool on);
    void updateIcomRC28Leds();
    void handleIcomRC28Tune(int steps);
    void refreshIcomRC28EncoderSettings();
    void snapIcomRC28FrequencyToKhz();
    void handleIcomRC28Button(int button, int action);

  private:
    MainWindow* m_window{nullptr};
};
