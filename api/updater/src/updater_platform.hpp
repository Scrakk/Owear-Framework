// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/updater/src/updater_platform.hpp — primitivas de plataforma para el
// reemplazo atómico del binario y el relanzamiento. Implementadas en
// updater_platform_<linux|mac|win>.cpp (selección por CMake, sin #ifdef).
//
#pragma once

#include <string>

namespace upd {

/// Ruta absoluta del ejecutable actualmente en ejecución. Vacía si falla.
std::string CurrentExePath();

/// Reemplaza `exe` por el contenido de `newFile` (mismo filesystem: rename
/// atómico; distinto: copy+rename) y relanza el proceso a partir de `exe`.
/// Solo retorna en caso de error (deja `error` con el motivo); si tiene
/// éxito, el proceso actual termina reemplazado por el nuevo binario.
bool ReplaceAndRelaunch(const std::string& newFile, const std::string& exe,
                        std::string& error);

} // namespace upd
