// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Runtime/NodeManager_win.cpp — spawn del sidecar (CreateProcess).
//
#include "NodeManager.hpp"
#include "../Control/ControlServer.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

namespace ow {

long NodeManager::Spawn(const std::filesystem::path& nodeBin,
                        const std::string& entryJs) {
    std::string sockPath = ControlServer::Get().SocketPath();
    if (!sockPath.empty()) SetEnvironmentVariableA("OW_CONTROL_SOCKET", sockPath.c_str());

    std::string cmdline = "\"" + nodeBin.string() + "\" \"" + entryJs + "\"";

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, FALSE,
                             0, nullptr, nullptr, &si, &pi);
    if (!ok) return -1;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<long>(pi.dwProcessId);
}

} // namespace ow
