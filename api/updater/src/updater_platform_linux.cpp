// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
#include "updater_platform.hpp"

#include <filesystem>
#include <unistd.h>

namespace upd {

namespace fs = std::filesystem;

std::string CurrentExePath() {
    char buf[4096] = {};
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return {};
    buf[n] = 0;
    return std::string(buf);
}

bool ReplaceAndRelaunch(const std::string& newFile, const std::string& exe,
                        std::string& error) {
    std::error_code ec;
    fs::rename(newFile, exe, ec); // reemplazo atómico si es el mismo filesystem
    if (ec) {
        // distinto filesystem → copiar+rename
        fs::copy_file(newFile, exe + ".new", fs::copy_options::overwrite_existing, ec);
        if (ec) { error = "copiado falló"; return false; }
        fs::rename(exe + ".new", exe, ec);
        if (ec) { error = "rename atómico falló"; return false; }
    }
    fs::permissions(exe,
                    fs::perms::owner_all | fs::perms::group_read |
                        fs::perms::group_exec | fs::perms::others_read |
                        fs::perms::others_exec,
                    ec);

    // execv requiere argv terminado en NULL; pasar nullptr directo es UB.
    char* argv[] = {const_cast<char*>(exe.c_str()), nullptr};
    execv(exe.c_str(), argv); // solo retorna si falla
    error = "execv falló";
    return false;
}

} // namespace upd
