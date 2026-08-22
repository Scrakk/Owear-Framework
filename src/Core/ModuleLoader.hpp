// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
// src/Core/ModuleLoader.hpp — carga de .owm (shared libraries) vía dlopen.
#pragma once

#include "ow_api.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ow {

class ModuleLoader {
public:
    /// Directorios donde buscar módulos, en orden.
    /// 1. $OW_MODULES_DIR (separado por ':')
    /// 2. <exe_dir>/modules
    static std::vector<std::filesystem::path> SearchPaths();

    /// Escanea los search paths y registra todos los .owm encontrados.
    /// Devuelve el total de funciones registradas.
    static size_t LoadAll();

    /// Carga un único archivo (.so/.dll/.dylib). Devuelve nº de funciones.
    static size_t LoadFile(const std::filesystem::path& file);

    /// Registra un módulo ya vinculado estáticamente (builtins).
    static size_t RegisterStatic(const ow_module_desc_t* desc);

    /// Libera todas las shared libraries (llamar antes de exit).
    static void Shutdown();
};

} // namespace ow
