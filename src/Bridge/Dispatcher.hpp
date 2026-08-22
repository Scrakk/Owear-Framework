// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
// src/Bridge/Dispatcher.hpp — registro y ejecución de funciones de módulos.
#pragma once

#include "ow_api.h"

#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace ow {

class Dispatcher {
public:
    static Dispatcher& Get();

    using ErrorSink = std::function<void(std::string_view msg)>;

    /// Registra todas las funciones de un descriptor. `origin` para logs.
    /// Devuelve el número de funciones registradas.
    size_t RegisterModule(const ow_module_desc_t* desc, std::string origin);

    /// Ejecuta una función. Rellena `res` SIEMPRE (ok o error interno).
    void Execute(uint32_t windowId, const std::string& module,
                 const std::string& fn, const ow_request_t* req,
                 ow_response_t* res);

    bool HasModule(const std::string& module) const;

    void SetErrorSink(ErrorSink sink);

    std::vector<std::pair<std::string, uint32_t>> ListModules() const;

private:
    Dispatcher() = default;
    mutable std::mutex mu_;
    std::map<std::string, const ow_fn_entry_t*> fns_; // "module/fn" → entry
    ErrorSink err_;
};

} // namespace ow
