// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
//
// ow/Json.h — acceso público al parser JSON del framework para módulos.
//

#include "ow/detail/minjson.hpp"

namespace ow::json {
using ow::json::Value;
using ow::json::Parse;
using ow::json::ParseResult;
using ow::json::JsLiteral;
} // namespace ow::json
