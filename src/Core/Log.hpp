// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
// src/Core/Log.hpp — logging minimalista del kernel.
#pragma once

#include <string_view>

namespace ow::log {

enum class Level : int { Debug = 0, Info, Warn, Error };

void Write(Level level, std::string_view scope, std::string_view msg);

inline void Debug(std::string_view scope, std::string_view msg) { Write(Level::Debug, scope, msg); }
inline void Info(std::string_view scope, std::string_view msg)  { Write(Level::Info, scope, msg); }
inline void Warn(std::string_view scope, std::string_view msg)  { Write(Level::Warn, scope, msg); }
inline void Error(std::string_view scope, std::string_view msg) { Write(Level::Error, scope, msg); }

} // namespace ow::log
