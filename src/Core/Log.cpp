// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
#include "Log.hpp"

#include <cstdio>
#include <ctime>
#include <mutex>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h> // GetSystemTime — MSVC no tiene clock_gettime
#endif

namespace ow::log {

namespace {
std::mutex g_mutex;
const char* Tag(Level l) {
    switch (l) {
    case Level::Debug: return "D";
    case Level::Info:  return "I";
    case Level::Warn:  return "W";
    case Level::Error: return "E";
    }
    return "?";
}
} // namespace

void Write(Level level, std::string_view scope, std::string_view msg) {
    std::lock_guard lock(g_mutex);
    char buf[16];
#ifdef _WIN32
    SYSTEMTIME st{};
    ::GetSystemTime(&st); // UTC — mismo reloj que epoch→HH:MM:SS en POSIX
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                  static_cast<int>(st.wHour), static_cast<int>(st.wMinute),
                  static_cast<int>(st.wSecond));
#else
    std::timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                  static_cast<int>((ts.tv_sec / 3600) % 24),
                  static_cast<int>((ts.tv_sec / 60) % 60),
                  static_cast<int>(ts.tv_sec % 60));
#endif
    std::fprintf(stderr, "[ow %s %s] %.*s: %.*s\n",
                 buf, Tag(level),
                 static_cast<int>(scope.size()), scope.data(),
                 static_cast<int>(msg.size()), msg.data());
}

} // namespace ow::log
