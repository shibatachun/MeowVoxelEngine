#pragma once

#include <spdlog/spdlog.h>

namespace MEngine::Core {

inline void initializeLogging()
{
    spdlog::set_pattern("[%T] [%^%l%$] %v");
    spdlog::set_level(spdlog::level::info);
}

} // namespace MEngine::Core

#define MENGINE_TRACE(...) SPDLOG_TRACE(__VA_ARGS__)
#define MENGINE_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#define MENGINE_INFO(...) SPDLOG_INFO(__VA_ARGS__)
#define MENGINE_WARN(...) SPDLOG_WARN(__VA_ARGS__)
#define MENGINE_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
