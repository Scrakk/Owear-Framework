// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Runtime/NodeManager_linux.cpp — spawn del sidecar (fork/exec).
//
#include "NodeManager.hpp"
#include "../Control/ControlServer.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>

namespace ow {

long NodeManager::Spawn(const std::filesystem::path& nodeBin,
                        const std::string& entryJs) {
    std::string bin = nodeBin.string();
    std::string binDir = nodeBin.parent_path().string();

    // PATH con el runtime gestionado al frente
    const char* oldPathC = std::getenv("PATH");
    std::string newPath = binDir + ":";
    if (oldPathC) newPath += oldPathC;

    std::string sockEnv;
    {
        auto& cs = ControlServer::Get();
        if (!cs.SocketPath().empty()) sockEnv = "OW_CONTROL_SOCKET=" + cs.SocketPath();
    }

    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        // hijo
        setenv("OW_CONTROL_SOCKET", ControlServer::Get().SocketPath().c_str(), 1);
        setenv("PATH", newPath.c_str(), 1);
        setenv("NODE_ENV", std::getenv("NODE_ENV") ? std::getenv("NODE_ENV") : "development",
               0);
        execl(bin.c_str(), bin.c_str(), entryJs.c_str(),
              static_cast<char*>(nullptr));
        _exit(127); // exec falló
    }
    return static_cast<long>(pid);
}

} // namespace ow
