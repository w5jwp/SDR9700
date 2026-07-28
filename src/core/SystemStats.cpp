#include "SystemStats.h"

#include <QtGlobal>

#if defined(Q_OS_LINUX)
#include <QFile>
#include <QList>
#elif defined(Q_OS_MAC)
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/processor_info.h>
#include <mach/task_info.h>
#endif

#include <algorithm>
#include <numeric>

SystemStats SystemStatsProvider::sample()
{
    SystemStats stats;
    const std::optional<CpuTicks> currentCpuTicks = readCpuTicks();
    if (currentCpuTicks)
    {
        if (m_previousCpuTicks)
        {
            stats.cpuPercent = calculateCpuPercent(*m_previousCpuTicks, *currentCpuTicks);
        }
        m_previousCpuTicks = currentCpuTicks;
    }
    stats.processResidentBytes = readProcessResidentBytes();
    return stats;
}

std::optional<double> SystemStatsProvider::calculateCpuPercent(const CpuTicks& previous, const CpuTicks& current)
{
    if (current.active < previous.active || current.idle < previous.idle)
    {
        return std::nullopt;
    }

    const quint64 activeDelta = current.active - previous.active;
    const quint64 idleDelta = current.idle - previous.idle;
    const quint64 totalDelta = activeDelta + idleDelta;
    if (totalDelta == 0)
    {
        return std::nullopt;
    }

    return qBound(0.0, 100.0 * static_cast<double>(activeDelta) / static_cast<double>(totalDelta), 100.0);
}

std::optional<CpuTicks> SystemStatsProvider::readCpuTicks() const
{
#if defined(Q_OS_LINUX)
    QFile file(QStringLiteral("/proc/stat"));
    if (!file.open(QIODevice::ReadOnly))
    {
        return std::nullopt;
    }

    const QList<QByteArray> parts = file.readLine().split(' ');
    QList<quint64> values;
    for (const QByteArray& part : parts)
    {
        if (!part.isEmpty() && part != "cpu")
        {
            values.append(part.trimmed().toULongLong());
        }
    }
    if (values.size() < 4)
    {
        return std::nullopt;
    }

    const quint64 idle = values[3] + (values.size() > 4 ? values[4] : 0);
    const quint64 total = std::accumulate(values.cbegin(), values.cend(), quint64{0});
    return CpuTicks{total - idle, idle};
#elif defined(Q_OS_MAC)
    natural_t cpuCount = 0;
    natural_t infoCount = 0;
    processor_info_array_t info = nullptr;
    const kern_return_t result =
        host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &cpuCount, &info, &infoCount);
    if (result != KERN_SUCCESS || !info)
    {
        return std::nullopt;
    }

    quint64 active = 0;
    quint64 idle = 0;
    const auto* loads = reinterpret_cast<processor_cpu_load_info_t>(info);
    for (natural_t cpu = 0; cpu < cpuCount; ++cpu)
    {
        active += loads[cpu].cpu_ticks[CPU_STATE_USER];
        active += loads[cpu].cpu_ticks[CPU_STATE_SYSTEM];
        active += loads[cpu].cpu_ticks[CPU_STATE_NICE];
        idle += loads[cpu].cpu_ticks[CPU_STATE_IDLE];
    }

    vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(info), infoCount * sizeof(integer_t));
    return CpuTicks{active, idle};
#else
    return std::nullopt;
#endif
}

std::optional<quint64> SystemStatsProvider::readProcessResidentBytes() const
{
#if defined(Q_OS_LINUX)
    QFile file(QStringLiteral("/proc/self/status"));
    if (!file.open(QIODevice::ReadOnly))
    {
        return std::nullopt;
    }

    const QList<QByteArray> lines = file.readAll().split('\n');
    const auto line =
        std::find_if(lines.cbegin(), lines.cend(), [](const QByteArray& value) { return value.startsWith("VmRSS:"); });
    if (line == lines.cend())
    {
        return std::nullopt;
    }

    const QList<QByteArray> parts = line->simplified().split(' ');
    if (parts.size() < 2)
    {
        return std::nullopt;
    }
    return parts[1].toULongLong() * 1024;
#elif defined(Q_OS_MAC)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
    const kern_return_t result =
        task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &infoCount);
    if (result != KERN_SUCCESS)
    {
        return std::nullopt;
    }
    return static_cast<quint64>(info.resident_size);
#else
    return std::nullopt;
#endif
}
