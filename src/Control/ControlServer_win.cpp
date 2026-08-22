// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Control/ControlServer_win.cpp — transporte named pipe de Windows.
// Ruta: \\.\pipe\owear-<pid>
//
#include "ControlServer.hpp"
#include "../Core/Log.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>
#include <map>
#include <string>

namespace ow {

namespace {

class PipeServer final : public ControlServer {
public:
    bool PlatformListen() override {
        char name[256];
        std::snprintf(name, sizeof(name), "\\\\.\\pipe\\owear-%lu",
                      static_cast<unsigned long>(GetCurrentProcessId()));
        socketPath_ = name;

        // pipe duplex por mensaje; aceptamos un cliente a la vez (SDK único)
        pipe_ = CreateNamedPipeA(
            name,
            PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            4,   // instancias
            64 * 1024, 64 * 1024, 0, nullptr);
        if (pipe_ == INVALID_HANDLE_VALUE) return false;

        // hilo lector: acepta conexiones y reenvía líneas al main loop
        readerThread_ = std::thread([this] { ReaderLoop(); });
        return true;
    }

    void PlatformSend(uint64_t clientId, std::string_view line) override {
        std::lock_guard lock(sendMu_);
        if (clientId == 0) {
            for (auto& [id, buf] : clients_) WriteClient(buf, line);
            return;
        }
        if (auto it = clients_.find(clientId); it != clients_.end())
            WriteClient(it->second, line);
    }

    void PlatformStop() override {
        running_ = false;
        if (pipe_ != INVALID_HANDLE_VALUE) {
            CancelIoEx(pipe_, nullptr);
            DisconnectNamedPipe(pipe_);
            CloseHandle(pipe_);
            pipe_ = INVALID_HANDLE_VALUE;
        }
        if (readerThread_.joinable()) readerThread_.join();
    }

private:
    struct ClientBuf {
        HANDLE handle = INVALID_HANDLE_VALUE;
        std::string pending;
    };

    static void WriteClient(ClientBuf& c, std::string_view line) {
        if (c.handle == INVALID_HANDLE_VALUE) return;
        DWORD written = 0;
        std::string out(line);
        out += '\n';
        WriteFile(c.handle, out.data(), static_cast<DWORD>(out.size()), &written, nullptr);
    }

    void AcceptOne() {
        if (!ConnectNamedPipe(pipe_, nullptr) &&
            GetLastError() != ERROR_PIPE_CONNECTED) {
            return;
        }
        uint64_t id = nextClientId_++;
        clients_[id] = ClientBuf{pipe_, {}};
        activeHandle_ = pipe_;

        // lee hasta desconexión (un cliente por instancia v1)
        char buf[8192];
        DWORD n = 0;
        while (running_) {
            if (!ReadFile(pipe_, buf, sizeof(buf), &n, nullptr) || n == 0) break;
            auto& acc = clients_[id].pending;
            acc.append(buf, n);
            size_t pos;
            while ((pos = acc.find('\n')) != std::string::npos) {
                std::string line = acc.substr(0, pos);
                acc.erase(0, pos + 1);
                // las respuestas se envían desde este hilo vía PlatformSend
                HandleLine(id, line);
            }
        }
        std::lock_guard lock(sendMu_);
        clients_.erase(id);
        DisconnectNamedPipe(pipe_);
        // recrea instancia para el siguiente cliente
        if (running_) {
            pipe_ = CreateNamedPipeA(
                socketPath_.c_str(),
                PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                4, 64 * 1024, 64 * 1024, 0, nullptr);
        }
    }

    void ReaderLoop() {
        while (running_) AcceptOne();
    }

    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    std::thread readerThread_;
    std::atomic<bool> running_{true};
    std::map<uint64_t, ClientBuf> clients_;
    std::mutex sendMu_;
    uint64_t nextClientId_ = 1;
    HANDLE activeHandle_ = INVALID_HANDLE_VALUE;
};

} // namespace

ControlServer& ControlServer::Get() {
    static PipeServer instance;
    return instance;
}

} // namespace ow
