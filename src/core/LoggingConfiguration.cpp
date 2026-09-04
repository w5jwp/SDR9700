#include "LoggingConfiguration.h"

#include <QLoggingCategory>
void LoggingConfiguration::applyBaseRules(const QString& rules)
{
    QLoggingCategory::setFilterRules(rules);
}
