// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Control/ControlServer.hpp — canal de control kernel ↔ SDK JS.
//
// Protocolo (NDJSON, una línea por mensaje):
//   request:  {"id":N,"cmd":"window.create","params":{...}}
//   response: {"id":N,"ok":true,"result":{...}} | {"id":N,"ok":false,"error":"..."}
//   event:    {"event":"window.event","params":{"windowId":N,"name":"resize","payload":null}}
//
// El transporte (UDS / named pipe) vive en ControlServer_<platform>.cpp.
//
#pragma once

#include "ow/Common.h"

#include <map>
#include <string>

namespace ow {

class Window;
std::map<WindowId, Window*>& LiveWindows();

class ControlServer {
public:
    /// Singleton de plataforma (definido en ControlServer_<plat>.cpp).
    static ControlServer& Get();

    bool Start();   // bind + listen + integración con el main loop
    void Stop();

    std::string SocketPath() const;

    /// Evento hacia todos los clientes conectados (SDK JS).
    void BroadcastEvent(const std::string& name, std::string_view paramsJson);

    // ── transporte (plataforma) ────────────────────────────────────
    /// Crea el endpoint escuchando. Devuelve false si el SO lo impide.
    virtual bool PlatformListen() = 0;
    /// Envía una línea NDJSON a un cliente (o broadcast si clientId == 0).
    virtual void PlatformSend(uint64_t clientId, std::string_view line) = 0;
    virtual void PlatformStop() = 0;

    /// El transporte llama esto por cada línea completa recibida.
    void HandleLine(uint64_t clientId, std::string_view line);
    /// El transporte llama al aceptar/descartar clientes.
    void HandleClientDisconnected(uint64_t clientId);

protected:
    std::string socketPath_;
    bool started_ = false;

    void SendResponse(uint64_t clientId, uint64_t id, bool ok,
                      std::string_view resultJson, std::string_view error = {});
    void SendLine(uint64_t clientId, std::string_view line);

private:
    bool HandleCommand(uint64_t clientId, uint64_t id, const std::string& cmd,
                       std::string_view paramsJson, std::string& resultJson,
                       std::string& error);
    void WireWindowEvents(WindowId id, Window* w);
};

} // namespace ow
