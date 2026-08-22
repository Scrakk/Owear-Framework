// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/ow_api.h — ABI-C estable del sistema de módulos Owear (.owm).
//
// Cada API vive en su propio folder (api/fs, api/process, …) y compila a una
// shared library independiente (.so/.dll/.dylib) que el kernel carga con
// dlopen. Actualización modular por API.
//
// Un módulo exporta EXACTAMENTE un símbolo obligatorio:
//     const ow_module_desc_t* ow_module_descriptor(void);
// y opcionalmente recibe el host con:
//     void ow_module_set_host(const ow_module_host_t* host);
//
// Contrato de memoria:
//  - Los buffers apuntados por res viven SOLO durante la llamada; el host
//    copia inmediatamente después de retornar.
//  - Prohibido lanzar excepciones hacia el host.
//
#pragma once

#include <stdint.h>
#include <stddef.h>

#if defined(_WIN32)
  #if defined(OW_MODULE_BUILD)
    #define OW_MODULE_EXPORT __declspec(dllexport)
  #else
    #define OW_MODULE_EXPORT
  #endif
#else
  #if defined(OW_MODULE_BUILD)
    #define OW_MODULE_EXPORT __attribute__((visibility("default")))
  #else
    #define OW_MODULE_EXPORT
  #endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ow_request {
    const char*  json;        ///< argumentos como array JSON. Nunca null ("[]").
    uint32_t     json_len;
    const uint8_t* bin;       ///< binario adjunto (puede ser null)
    uint32_t     bin_len;
    uint32_t     window_id;   ///< ventana invocante (0 = sin ventana)
    uint32_t     reserved;
    void*        host;        ///< contexto del kernel (opaco para el módulo)
} ow_request_t;

typedef struct ow_response {
    int32_t      status;      ///< 0 = ok; != 0 = error
    const char*  error;       ///< mensaje de error (si status != 0)
    const char*  json;        ///< resultado JSON ("null" permitido)
    uint32_t     json_len;
    const uint8_t* bin;       ///< binario de salida (opcional)
    uint32_t     bin_len;
} ow_response_t;

typedef void (*ow_fn_t)(const ow_request_t* req, ow_response_t* res);

typedef struct ow_fn_entry {
    const char* name;
    ow_fn_t     fn;
} ow_fn_entry_t;

typedef struct ow_module_desc {
    const char*          name;     ///< "fs", "process", "dialog", …
    const char*          version;
    const ow_fn_entry_t* fns;
    uint32_t             fn_count;
} ow_module_desc_t;

/// Único símbolo que un .owm debe exportar.
typedef const ow_module_desc_t* (*ow_module_entry_t)(void);

// ── Host callbacks (F-infra) ────────────────────────────────────────────────
// El kernel pasa esta tabla vía ow_module_set_host() justo después del dlopen.
// Permite a los módulos emitir eventos (watchers, PTY stdout, tray clicks…).

#define OW_HOST_ABI_VERSION 1

typedef void (*ow_host_emit_event_t)(void* ctx, uint32_t window_id,
                                     const char* name, const char* json);
typedef void (*ow_host_log_t)(void* ctx, int level, const char* msg);

typedef struct ow_module_host {
    uint32_t version;              ///< OW_HOST_ABI_VERSION
    void*    ctx;                  ///< contexto opaco del kernel
    ow_host_emit_event_t emit_event;
    ///<  window_id == 0 → broadcast (todas las ventanas + SDK por control socket)
    ow_host_log_t log;             ///< level: 0 debug · 1 info · 2 warn · 3 error
    void*    reserved[4];
} ow_module_host_t;

/// Opcional: el módulo guarda la tabla si quiere emitir eventos.
typedef void (*ow_module_set_host_t)(const ow_module_host_t* host);

#ifdef __cplusplus
} // extern "C"
#endif
