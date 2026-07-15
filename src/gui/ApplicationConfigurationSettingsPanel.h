#pragma once

#include <QString>
#include <QWidget>

class ApplicationConfigurationSettingsPanel : public QWidget
{
    Q_OBJECT

  public:
    explicit ApplicationConfigurationSettingsPanel(QWidget* parent = nullptr);

  signals:
    void memoriesChanged(const QString& message);
};
