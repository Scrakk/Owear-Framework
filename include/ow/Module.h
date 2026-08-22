// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
//
// ow/Module.h — API de usuario para escribir módulos nativos (.owm).
//
// Ejemplo (native/files.cpp):
//
//   #include <ow/Module.h>
//   #include <ow/Json.h>
//
//   static void readFile(const ow_request_t* req, ow_response_t* res) {
//     auto args = ow::json::Parse(req->json, req->json_len);
//     ...
//     ow::Module::RespondOk(res, "\"hola\"");
//   }
//
//   OW_MODULE_BEGIN(files, "1.0.0")
//     OW_FN(readFile)
//   OW_MODULE_END()
//
// El loader del kernel busca `ow_module_descriptor` con dlopen y registra
// las funciones en el dispatcher del bridge.
//

#include "ow/Common.h"
#include "ow_api.h"

#define OW_MODULE_EXPORT_SYMBOL ow_module_descriptor

/// Abre la tabla de funciones del módulo.
#define OW_MODULE_BEGIN(Name, Version)                                        \
    static constexpr const char* _ow_mod_name = #Name;                        \
    static constexpr const char* _ow_mod_version = Version;                   \
    static const ::ow_fn_entry_t _ow_fn_table[] = {

#define OW_FN(FnName) { #FnName, &FnName },

#define OW_MODULE_END()                                                       \
    };                                                                        \
    static const ::ow_module_desc_t _ow_module_desc {                         \
        _ow_mod_name, _ow_mod_version,                                        \
        _ow_fn_table,                                                         \
        static_cast<uint32_t>(sizeof(_ow_fn_table) / sizeof(_ow_fn_table[0])) \
    };                                                                        \
    extern "C" OW_MODULE_EXPORT const ::ow_module_desc_t*                     \
    ow_module_descriptor(void) { return &_ow_module_desc; }

namespace ow::Module {

/// Helpers de respuesta (copian a buffers estáticos del módulo NO — ver nota).
/// RespondOk copia `json` a un buffer interno thread-local que vive hasta la
/// próxima llamada de ese hilo; suficiente porque el host copia al instante.

inline void RespondOk(ow_response_t* res, std::string_view json,
                      const uint8_t* bin = nullptr, uint32_t binLen = 0) {
    static thread_local std::string buf;
    buf.assign(json);
    res->status = 0;
    res->error = nullptr;
    res->json = buf.c_str();
    res->json_len = static_cast<uint32_t>(buf.size());
    res->bin = bin;
    res->bin_len = binLen;
}

inline void RespondError(ow_response_t* res, std::string_view message) {
    static thread_local std::string buf;
    buf.assign(message);
    res->status = 1;
    res->error = buf.c_str();
    res->json = "null";
    res->json_len = 4;
    res->bin = nullptr;
    res->bin_len = 0;
}

} // namespace ow::Module
