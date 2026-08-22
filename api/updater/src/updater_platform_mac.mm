// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
#include "updater_platform.hpp"

#include <filesystem>
#include <mach-o/dyld.h>
#include <unistd.h>

namespace upd {

namespace fs = std::filesystem;

std::string CurrentExePath() {
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) return {};
    std::error_code ec;
    auto real = fs::canonical(buf, ec);
    return ec ? std::string(buf) : real.string();
}

bool ReplaceAndRelaunch(const std::string& newFile, const std::string& exe,
                        std::string& error) {
    std::error_code ec;
    fs::rename(newFile, exe, ec); // POSIX: rename de un ejecutable en uso es válido
    if (ec) {
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

    char* argv[] = {const_cast<char*>(exe.c_str()), nullptr};
    execv(exe.c_str(), argv); // solo retorna si falla
    error = "execv falló";
    return false;
}

} // namespace upd
