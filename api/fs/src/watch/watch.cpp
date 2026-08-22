// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/fs/src/watch/watch.cpp — registro de watchers + batching de eventos.
// El backend por plataforma (watch_<plat>) entrega eventos crudos aquí.
//
#include "watch.hpp"
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#include <filesystem>

#include <mutex>
#include <set>

namespace fswatch {

using ow::json::Array;
using ow::json::Object;
using ow::json::Value;

namespace {
std::mutex g_mu;
std::map<int, Watcher> g_watchers;
int g_nextId = 1;
const ow_module_host_t* g_host = nullptr;
uint32_t g_sourceWindow = 0; // ventana que registró (para dirigir eventos)
} // namespace

void SetHost(const ow_module_host_t* host) { g_host = host; }

int Create(std::string path, bool recursive, uint32_t windowId) {
    std::lock_guard lock(g_mu);
    int id = g_nextId++;
    auto& w = g_watchers[id];
    w.id = id;
    w.path = std::move(path);
    w.recursive = recursive;
    w.window_id = windowId ? windowId : g_sourceWindow;
    return id;
}

Watcher* Get(int id) {
    auto it = g_watchers.find(id);
    return it == g_watchers.end() ? nullptr : &it->second;
}

void Destroy(int id) {
    std::lock_guard lock(g_mu);
    g_watchers.erase(id);
}

std::set<int> AllIds() {
    std::lock_guard lock(g_mu);
    std::set<int> ids;
    for (auto& [id, _] : g_watchers) ids.insert(id);
    return ids;
}

/// Batching: acumula eventos por watcher y emite cada ~50 ms desde el hilo
/// del backend. Llamar desde el hilo lector del watcher.
void FlushEvents(int watcherId, std::vector<Event>& events) {
    const ow_module_host_t* host = g_host;
    if (!host || !host->emit_event || events.empty()) return;

    uint32_t win = 0;
    {
        std::lock_guard lock(g_mu);
        auto it = g_watchers.find(watcherId);
        if (it == g_watchers.end()) return;
        win = it->second.window_id;
    }

    Array arr;
    arr.reserve(events.size());
    for (auto& e : events) {
        Object o;
        o.emplace_back("type", Value(e.type));
        o.emplace_back("path", Value(std::move(e.path)));
        arr.emplace_back(Value(std::move(o)));
    }
    Object payload;
    payload.emplace_back("watcherId", Value(static_cast<int64_t>(watcherId)));
    payload.emplace_back("events", Value(std::move(arr)));

    std::string json = Value(std::move(payload)).Serialize();
    host->emit_event(host->ctx, win, "fs.watch", json.c_str());
    events.clear();
}

} // namespace fswatch

// ── funciones expuestas por el módulo ────────────────────────────────────────

namespace fsimpl {

namespace fs = std::filesystem;
using ow::json::Array;
using ow::json::Object;
using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

void watch(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() ||
        parsed.value->AsArray().empty() ||
        !parsed.value->AsArray()[0].IsString())
        return RespondError(res, "path requerido");

    std::string path = parsed.value->AsArray()[0].AsString();
    bool recursive = parsed.value->AsArray().size() > 1 &&
                     parsed.value->AsArray()[1].IsBool()
                         ? parsed.value->AsArray()[1].AsBool()
                         : false;

    std::error_code ec;
    if (!fs::is_directory(path, ec))
        return RespondError(res, "no es directorio: " + path);

    int id = fswatch::Create(path, recursive, req->window_id);
    std::string err;
    if (!fswatch::Start(id, err)) {
        fswatch::Destroy(id);
        return RespondError(res, err.empty() ? "no se pudo iniciar el watcher" : err);
    }
    RespondOk(res, Value(static_cast<int64_t>(id)).Serialize().c_str());
}

void unwatch(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value) return RespondError(res, "args inválidos");
    if (!parsed.value->IsArray() || parsed.value->AsArray().empty() ||
        !parsed.value->AsArray()[0].IsNumber())
        return RespondError(res, "watcherId requerido");
    int id = static_cast<int>(parsed.value->AsArray()[0].AsInt());
    fswatch::Stop(id);
    fswatch::Destroy(id);
    RespondOk(res, "null");
}

} // namespace fsimpl

const ow_fn_entry_t kWatchFns[] = {
    {"watch", &fsimpl::watch},
    {"unwatch", &fsimpl::unwatch},
};

namespace fsimpl {
const uint32_t kWatchFnCount = sizeof(kWatchFns) / sizeof(kWatchFns[0]);
} // namespace fsimpl
