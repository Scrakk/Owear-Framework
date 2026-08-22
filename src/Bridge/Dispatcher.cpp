// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
#include "Dispatcher.hpp"

#include "../Core/Log.hpp"

namespace ow {

Dispatcher& Dispatcher::Get() {
    static Dispatcher d;
    return d;
}

size_t Dispatcher::RegisterModule(const ow_module_desc_t* desc, std::string origin) {
    if (!desc || !desc->name || !desc->fns) {
        log::Warn("dispatcher", "descriptor inválido desde " + origin);
        return 0;
    }
    size_t n = 0;
    std::lock_guard lock(mu_);
    for (uint32_t i = 0; i < desc->fn_count; ++i) {
        const auto& e = desc->fns[i];
        if (!e.name || !e.fn) continue;
        fns_[std::string(desc->name) + "/" + e.name] = &e;
        ++n;
    }
    log::Info("dispatcher", "módulo '" + std::string(desc->name) + "' registrado (" +
                                std::to_string(n) + " fn, " + origin + ")");
    return n;
}

void Dispatcher::Execute(uint32_t windowId, const std::string& module,
                         const std::string& fn, const ow_request_t* req,
                         ow_response_t* res) {
    const ow_fn_entry_t* entry = nullptr;
    ErrorSink errSink;
    {
        std::lock_guard lock(mu_);
        auto it = fns_.find(module + "/" + fn);
        if (it != fns_.end()) entry = it->second;
        errSink = err_;
    }

    res->status = 1;
    res->error = "";
    res->json = "null";
    res->json_len = 4;
    res->bin = nullptr;
    res->bin_len = 0;

    if (!entry) {
        static thread_local std::string msg;
        msg = "función desconocida: " + module + "/" + fn;
        res->error = msg.c_str();
        if (errSink) errSink(msg);
        return;
    }

    ow_request_t r{};
    if (req) r = *req;
    r.window_id = windowId;

    try {
        entry->fn(&r, res);
    } catch (const std::exception& e) {
        static thread_local std::string msg;
        msg = std::string("excepción en módulo: ") + e.what();
        res->status = 2;
        res->error = msg.c_str();
        res->json = "null";
        res->json_len = 4;
        if (errSink) errSink(msg);
    } catch (...) {
        static thread_local std::string msg;
        msg = "excepción desconocida en módulo";
        res->status = 2;
        res->error = msg.c_str();
        res->json = "null";
        res->json_len = 4;
        if (errSink) errSink(msg);
    }

    // Normaliza respuesta vacía
    if (res->status == 0 && !res->json) {
        res->json = "null";
        res->json_len = 4;
    }
}

bool Dispatcher::HasModule(const std::string& module) const {
    std::lock_guard lock(mu_);
    auto prefix = module + "/";
    for (const auto& [key, _] : fns_)
        if (key.compare(0, prefix.size(), prefix) == 0) return true;
    return false;
}

std::vector<std::pair<std::string, uint32_t>> Dispatcher::ListModules() const {
    std::lock_guard lock(mu_);
    std::map<std::string, uint32_t> counts;
    for (const auto& [key, _] : fns_) {
        auto slash = key.find('/');
        counts[key.substr(0, slash)]++;
    }
    return {counts.begin(), counts.end()};
}

void Dispatcher::SetErrorSink(ErrorSink sink) {
    std::lock_guard lock(mu_);
    err_ = std::move(sink);
}

} // namespace ow
