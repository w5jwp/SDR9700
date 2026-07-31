#include "LoggingConfiguration.h"

#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <optional>

namespace
{
QMutex loggingRulesMutex;
QString baseLoggingRules;
std::optional<bool> civDataOverride;

void applyRulesLocked()
{
    QString rules = baseLoggingRules;
    if (civDataOverride.has_value())
    {
        rules.append(QStringLiteral("\nci-v.debug=%1\nci-v.info=%1")
                         .arg(*civDataOverride ? QStringLiteral("true") : QStringLiteral("false")));
    }
    QLoggingCategory::setFilterRules(rules);
}
} // namespace

void LoggingConfiguration::applyBaseRules(const QString& rules)
{
    QMutexLocker lock(&loggingRulesMutex);
    baseLoggingRules = rules;
    applyRulesLocked();
}

void LoggingConfiguration::setCivDataEnabled(bool enabled)
{
    QMutexLocker lock(&loggingRulesMutex);
    civDataOverride = enabled;
    applyRulesLocked();
}
