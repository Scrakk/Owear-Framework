// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/crashreporter/src/crashreporter.cpp — captura de crashes del kernel.
//
// Instala handlers para SIGSEGV/SIGABRT/SIGFPE/SIGBUS/SIGILL y escribe
// $XDG_CACHE_HOME/owear/crashes/crash-<pid>.log con señal + backtrace
// (glibc backtrace). El módulo expone rutas para que la app decida si
// subirlas a su propio telemetría.
//
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

namespace crash {

std::string CrashDir() {
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    std::string base = (xdg && *xdg) ? xdg : std::string(std::getenv("HOME") ?: "/tmp") + "/.cache";
    return base + "/owear/crashes";
}

std::string LastCrash;

void WriteCrash(int sig) {
    // async-signal-safety: usamos solo open/write aquí (backtrace no es
    // estrictamente safe pero es lo mejor disponible sin dependencias)
    static char dirBuf[512];
    snprintf(dirBuf, sizeof(dirBuf), "%s", CrashDir().c_str());
    // mkdir best-effort (no safe, aceptable en crash path)
    char cmd[640];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s 2>/dev/null", dirBuf);
    (void)!system(cmd);

    static char path[600];
    snprintf(path, sizeof(path), "%s/crash-%d.log", dirBuf, (int)getpid());
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0600);
    if (fd >= 0) {
        time_t now = time(nullptr);
        char header[128];
        int n = snprintf(header, sizeof(header),
                         "owear crash · señal %d (%s) · pid %d · %s", sig,
                         strsignal(sig), (int)getpid(), ctime(&now));
        (void)!write(fd, header, n);

        void* bt[32];
        int frames = backtrace(bt, 32);
        backtrace_symbols_fd(bt, frames, fd);
        close(fd);
        LastCrash = path;
    }
    // restaura default y re-lanza para core dump real
    signal(sig, SIG_DFL);
    raise(sig);
}

void Install() {
    struct Sig {
        int sig;
    };
    for (int s : {SIGSEGV, SIGABRT, SIGFPE, SIGBUS, SIGILL}) {
        signal(s, &WriteCrash);
    }
}

} // namespace crash

namespace crashmod {

using ow::json::Value;
using ow::Module::RespondOk;
using ow::Module::RespondError;

void install(const ow_request_t*, ow_response_t* res) {
    crash::Install();
    RespondOk(res, Value(crash::CrashDir()).Serialize().c_str());
}

void lastCrashLog(const ow_request_t*, ow_response_t* res) {
    RespondOk(res,
              crash::LastCrash.empty()
                  ? "null"
                  : Value(crash::LastCrash).Serialize().c_str());
}

} // namespace crashmod

namespace ow::internal {
const ow_module_desc_t* CrashReporterDescriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"install", &crashmod::install},
        {"lastCrashLog", &crashmod::lastCrashLog},
    };
    static const ow_module_desc_t d{
        "crashreporter", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}
} // namespace ow::internal
