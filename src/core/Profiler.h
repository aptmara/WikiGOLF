#pragma once
/**
 * @file Profiler.h
 * @brief パフォーマンス計測
 */

#include <string>
#include <chrono>
#include <unordered_map>

namespace core {

class Profiler {
public:
    static Profiler& Instance();

    void BeginScope(const std::string& name);
    void EndScope(const std::string& name);
    void ReportAndReset(float dt);

private:
    Profiler() = default;
    ~Profiler() = default;

    struct PerfData {
        std::chrono::high_resolution_clock::time_point startTime;
        double accumulatedTime = 0.0; // in milliseconds
        int callCount = 0;
    };

    std::unordered_map<std::string, PerfData> m_data;
    double m_elapsedSinceLastReport = 0.0;
};

class ScopedTimer {
public:
    ScopedTimer(const std::string& name);
    ~ScopedTimer();
private:
    std::string m_name;
};

} // namespace core

#define PROFILE_SCOPE(name) core::ScopedTimer timer_##__LINE__(name)
