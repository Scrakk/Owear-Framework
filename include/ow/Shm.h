// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// ow/Shm.h — memoria compartida kernel ↔ WebView SIN COPIA (F3).
//
// Un módulo nativo pone bytes en una región registrada; el renderer los lee
// con `ow.readShared({id, size})` → ArrayBuffer vía el scheme `ow-shm://`.
// El kernel sirve la región mapeada directamente (g_bytes_new_static /
// IStream / NSData sin copiar) — nada pasa por JSON ni base64.
//
// Para módulos .owm externos: estos símbolos se resuelven desde el binario
// del host (el kernel enlaza con -rdynamic / /EXPORT). Declarados aquí como
// funciones C puras.
//
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef _WIN32
  #ifdef OW_MODULE_BUILD
    #define OW_SHM_API __declspec(dllimport)
  #else
    #define OW_SHM_API
  #endif
#else
  #define OW_SHM_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/// Registra `len` bytes (los copia UNA vez al crear la región mmap).
/// Devuelve id hex (16 chars) o "" en fallo.
OW_SHM_API const char* ow_shm_put(const uint8_t* data, size_t len);

/// Datos vivos de una región (para schemes del kernel). NULL si no existe.
OW_SHM_API const uint8_t* ow_shm_data(const char* id, size_t* out_len);

/// Libera TODAS las regiones (llamado por el kernel al salir).
OW_SHM_API void ow_shm_shutdown(void);

#ifdef __cplusplus
} // extern "C"
#endif
