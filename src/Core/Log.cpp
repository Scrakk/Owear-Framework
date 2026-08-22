// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
#include "Log.hpp"

#include <cstdio>
#include <ctime>
#include <mutex>

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
    std::timespec ts{};
    std::timespec tm{};
    clock_gettime(CLOCK_REALTIME, &ts);
    tm = ts;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                  static_cast<int>((ts.tv_sec / 3600) % 24),
                  static_cast<int>((ts.tv_sec / 60) % 60),
                  static_cast<int>(ts.tv_sec % 60));
    std::fprintf(stderr, "[ow %s %s] %.*s: %.*s\n",
                 buf, Tag(level),
                 static_cast<int>(scope.size()), scope.data(),
                 static_cast<int>(msg.size()), msg.data());
}

} // namespace ow::log
