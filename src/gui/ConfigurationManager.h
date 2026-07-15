#pragma once

#include <QString>

class QWidget;

struct MemoryImportResult
{
    bool completed{false};
    bool success{false};
    int importedCount{0};
};

class ConfigurationManager
{
  public:
    static QString configPath();
    static QString memoriesPath();
    static bool backupMemories(QWidget* parent);
    static MemoryImportResult restoreMemories(QWidget* parent);
    static bool exportMemories(QWidget* parent);
    static MemoryImportResult importMemories(QWidget* parent);
    static bool resetMemories(QWidget* parent);
    static bool backupConfiguration(QWidget* parent);
    static bool restoreConfigurationAndRestart(QWidget* parent);
    static bool exportConfiguration(QWidget* parent);
    static bool importConfigurationAndRestart(QWidget* parent);
    static bool resetConfigurationAndRestart(QWidget* parent);
};
