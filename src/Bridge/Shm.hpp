// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
// src/Bridge/Shm.hpp — interno del kernel (la API pública es ow/Shm.h).
#pragma once

#include <cstdint>
#include <cstddef>

namespace ow::shm {

/// Registra bytes y devuelve el id de región ("" en fallo).
const char* Put(const uint8_t* data, size_t len);

/// Puntero vivo a los datos de una región. NULL si no existe.
const uint8_t* Data(const char* id, size_t* out_len);

void Shutdown();
size_t Count();

} // namespace ow::shm
