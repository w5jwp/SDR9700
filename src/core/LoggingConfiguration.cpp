#include "LoggingConfiguration.h"

#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>

namespace
{
QMutex rulesMutex;
QString baseRules;
bool applicationCivTrafficEnabled{false};

void applyRules()
{
    QString rules = baseRules;
    if (applicationCivTrafficEnabled)
    {
        rules.append(QStringLiteral("\nci-v.debug=true\nci-v.info=true"));
    }
    QLoggingCategory::setFilterRules(rules);
}
} // namespace

void LoggingConfiguration::applyBaseRules(const QString& rules)
{
    QMutexLocker lock(&rulesMutex);
    baseRules = rules;
    applyRules();
}

void LoggingConfiguration::setApplicationCivTrafficEnabled(bool enabled)
{
    QMutexLocker lock(&rulesMutex);
    applicationCivTrafficEnabled = enabled;
    applyRules();
}
