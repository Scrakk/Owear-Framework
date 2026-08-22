// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
// src/Runtime/Http.hpp — descargas con libcurl.
#pragma once

#include <filesystem>
#include <string>

namespace ow::http {

/// Descarga a archivo. Crea directorios padre. Sigue redirects.
bool DownloadToFile(const std::string& url, const std::filesystem::path& dest,
                    std::string& error);

/// Descarga a memoria (máx ~64 MB por seguridad).
bool DownloadToString(const std::string& url, std::string& out, std::string& error);

} // namespace ow::http
