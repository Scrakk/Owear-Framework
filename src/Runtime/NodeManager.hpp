// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Runtime/NodeManager.hpp — gestor del runtime Node.js.
//
// SIN Node embebido: descarga la release oficial (nodejs.org/dist), verifica
// SHA256 contra SHASUMS256.txt y la cachea por versión. Spawn como sidecar.
//
#pragma once

#include "ow/Common.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ow {

class NodeManager {
public:
    /// Garantiza un binario node que satisfaga `range` ("latest"|"lts"|"v22"|...).
    /// Devuelve la ruta al ejecutable node.
    static Result<std::filesystem::path> Ensure(const std::string& range);

    /// Lanza `node <entryJs>` como proceso hijo (sidecar).
    /// Env: OW_CONTROL_SOCKET, PATH con node al frente. Devuelve pid o -1.
    static long Spawn(const std::filesystem::path& nodeBin, const std::string& entryJs);

    /// Directorio cache (XDG).
    static std::filesystem::path CacheRoot();

    /// Runtime ya disponible (cache o sistema) sin descargar.
    static std::optional<std::filesystem::path> FindCached();

private:
    struct Release {
        std::string version;   // "v22.4.0"
        bool lts = false;
    };
    static bool FetchIndex(std::vector<Release>& out, std::string& error);
    static const Release* Pick(const std::vector<Release>& list,
                               const std::string& range);
    static std::string PlatformTag();
    static std::string ArchTag();
};

} // namespace ow
