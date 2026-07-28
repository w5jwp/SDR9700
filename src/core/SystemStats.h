#pragma once

#include <QtGlobal>

#include <optional>

struct CpuTicks
{
    quint64 active{0};
    quint64 idle{0};
};

struct SystemStats
{
    std::optional<double> cpuPercent;
    std::optional<quint64> processResidentBytes;
};

class SystemStatsProvider
{
  public:
    SystemStats sample();

    static std::optional<double> calculateCpuPercent(const CpuTicks& previous, const CpuTicks& current);

  private:
    std::optional<CpuTicks> readCpuTicks() const;
    std::optional<quint64> readProcessResidentBytes() const;

    std::optional<CpuTicks> m_previousCpuTicks;
};
