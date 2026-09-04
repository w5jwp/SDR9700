#pragma once

#include <QString>

class LoggingConfiguration
{
  public:
    static void applyBaseRules(const QString& rules);
    static void setApplicationCivTrafficEnabled(bool enabled);
};
