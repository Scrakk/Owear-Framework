// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
// src/Runtime/TarGz.hpp — extractor tar.gz en streaming (ustar/GNU).
#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace ow::archive {

/// Extrae un .tar.gz en `destDir`. Callback opcional por entrada extraída.
/// Ignora symlinks/hardlinks (node tarball: solo necesitamos archivos reales).
/// Devuelve false y `error` en fallo.
bool ExtractTarGz(const std::filesystem::path& tarGz,
                  const std::filesystem::path& destDir,
                  std::string& error,
                  const std::function<void(const std::string& relPath)>& onEntry = nullptr);

} // namespace ow::archive
