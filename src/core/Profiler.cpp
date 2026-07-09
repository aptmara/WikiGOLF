/**
 * @file Profiler.cpp
 * @brief パフォーマンス計測の実装
 */

#include "Profiler.h"
#include "Logger.h"

namespace core {

Profiler& Profiler::Instance() {
    static Profiler instance;
    return instance;
}

void Profiler::BeginScope(const std::string& name) {
    m_data[name].startTime = std::chrono::high_resolution_clock::now();
}

void Profiler::EndScope(const std::string& name) {
    auto endTime = std::chrono::high_resolution_clock::now();
    auto& data = m_data[name];
    std::chrono::duration<double, std::milli> elapsed = endTime - data.startTime;
    data.accumulatedTime += elapsed.count();
    data.callCount++;
}

void Profiler::ReportAndReset(float dt) {
    m_elapsedSinceLastReport += dt;
    if (m_elapsedSinceLastReport >= 1.0) { // 1秒ごとにログ出力
        LOG_INFO("Perf", "--- Performance Report ---");
        for (auto& pair : m_data) {
            auto& data = pair.second;
            if (data.callCount > 0) {
                double avgTime = data.accumulatedTime / data.callCount;
                LOG_INFO("Perf", "[{}] Avg: {:.3f}ms, Total: {:.3f}ms, Calls: {}", 
                         pair.first.c_str(), avgTime, data.accumulatedTime, data.callCount);
            }
            data.accumulatedTime = 0.0;
            data.callCount = 0;
        }
        LOG_INFO("Perf", "--------------------------");
        m_elapsedSinceLastReport = 0.0;
    }
}

ScopedTimer::ScopedTimer(const std::string& name) : m_name(name) {
    Profiler::Instance().BeginScope(m_name);
}

ScopedTimer::~ScopedTimer() {
    Profiler::Instance().EndScope(m_name);
}

} // namespace core
