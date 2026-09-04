#include "LoggingConfiguration.h"

#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>

namespace
{
QMutex loggingRulesMutex;
} // namespace

void LoggingConfiguration::applyBaseRules(const QString& rules)
{
    QMutexLocker lock(&loggingRulesMutex);
    QLoggingCategory::setFilterRules(rules);
}
