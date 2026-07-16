#pragma once

#include <QString>

class QWidget;

class ConfigurationManager
{
  public:
    static QString configPath();
    static bool backupConfiguration(QWidget* parent);
    static bool restoreConfigurationAndRestart(QWidget* parent);
    static bool resetConfigurationAndRestart(QWidget* parent);
};
