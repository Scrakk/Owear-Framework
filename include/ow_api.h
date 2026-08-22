// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/ow_api.h — ABI-C estable del sistema de módulos Owear (.owm).
//
// Este es el ÚNICO contrato entre el kernel y las shared libraries de módulos.
// Estilo innerta_api.h: extern "C", structs planos, sin excepciones a través
// del límite. Un módulo exporta exactamente un símbolo:
//
//     const ow_module_desc_t* ow_module_descriptor(void);
//
// Contrato de memoria:
//  - El host llama a la función con (req, res).
//  - Los buffers apuntados por res deben permanecer válidos SOLO durante la
//    llamada; el host copia inmediatamente después de que la función retorna.
//  - La función NUNCA debe lanzar excepciones hacia el host (catch-all dentro).
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
    const char*          name;     ///< "fs", "dialog", "my-module"...
    const char*          version;
    const ow_fn_entry_t* fns;
    uint32_t             fn_count;
} ow_module_desc_t;

/// Único símbolo que un .owm debe exportar.
typedef const ow_module_desc_t* (*ow_module_entry_t)(void);

#ifdef __cplusplus
} // extern "C"
#endif
