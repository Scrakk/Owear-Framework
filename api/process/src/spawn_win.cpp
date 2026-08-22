// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/process/src/spawn_win.cpp — CreateProcess con pipes anónimos.
// VERIFICAR-EN-WINDOWS.
//
#include "registry.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>

namespace proc {

namespace {
std::wstring Utf8(std::string_view s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

void PipeReader(int id, HANDLE pipe, uint32_t win, const char* which,
                const std::atomic<bool>& stop) {
    char buf[65 * 1024];
    DWORD n = 0;
    while (!stop.load()) {
        if (!ReadFile(pipe, buf, sizeof(buf), &n, nullptr) || n == 0) break;
        EmitData(win, id, which, buf, n);
    }
    CloseHandle(pipe);
}
} // namespace

long SpawnPipes(int* outId, const std::string& cmd, std::vector<std::string> args,
                const std::string& cwd, std::map<std::string, std::string> env,
                bool useShell, uint32_t windowId, std::string& err) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE inR = nullptr, inW = nullptr, outR = nullptr, outW = nullptr, errR = nullptr, errW = nullptr;
    if (!CreatePipe(&outR, &outW, &sa, 0) || !CreatePipe(&errR, &errW, &sa, 0) ||
        !CreatePipe(&inR, &inW, &sa, 0)) {
        err = "CreatePipe falló";
        return -1;
    }
    SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errR, HANDLE_FLAG_INHERIT, 0);

    std::string cmdline = cmd;
    for (auto& a : args) cmdline += " \"" + a + "\"";
    if (useShell) cmdline = "cmd.exe /c " + cmdline;

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inR;
    si.hStdOutput = outW;
    si.hStdError = errW;

    // env block
    std::string envBlock;
    for (auto& [k, v] : env) {
        envBlock += k;
        envBlock.push_back('=');
        envBlock += v;
        envBlock.push_back('\0');
    }
    if (!envBlock.empty()) envBlock.push_back('\0');

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, TRUE,
                             0, envBlock.empty() ? nullptr : envBlock.data(),
                             cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    CloseHandle(inR); CloseHandle(outW); CloseHandle(errW);
    if (!ok) {
        CloseHandle(inW); CloseHandle(outR); CloseHandle(errR);
        err = "CreateProcess falló";
        return -1;
    }

    std::lock_guard lock(g_mu);
    int id = g_nextId++;
    auto* p = new Proc();
    p->id = id; p->pid = (pid_t)pi.dwProcessId;
    p->handle = pi.hProcess; p->stdinHandle = inW;
    p->window_id = windowId;
    *outId = id;
    g_procs[id] = p;

    auto* stopFlag = &p->stop;
    p->reader = std::thread([id, outR, errR, win = p->window_id, hProc = pi.hProcess, stopFlag] {
        PipeReader(id, outR, win, "stdout", *stopFlag);
        PipeReader(id, errR, win, "stderr", *stopFlag);
        WaitForSingleObject(hProc, INFINITE);
        DWORD code = -1;
        GetExitCodeProcess(hProc, &code);
        CloseHandle(hProc);
        Emit(win, "process.exit",
             "{\"procId\":" + std::to_string(id) + ",\"code\":" +
                 std::to_string((int)code) + ",\"signal\":0}");
        Remove(id);
    });
    return (long)pi.dwProcessId;
}

bool WriteStdin(int id, const char* data, size_t len, std::string& err) {
    auto* p = Get(id);
    if (!p || !p->stdinHandle) { err = "proc sin stdin"; return false; }
    DWORD written = 0;
    if (!WriteFile(p->stdinHandle, data, (DWORD)len, &written, nullptr)) {
        err = "WriteFile falló"; return false;
    }
    return true;
}

void CloseStdin(int id) {
    auto* p = Get(id);
    if (p && p->stdinHandle) { CloseHandle(p->stdinHandle); p->stdinHandle = nullptr; }
}

bool Kill(int id, int, std::string& err) {
    auto* p = Get(id);
    if (!p || !p->handle) { err = "proc inexistente"; return false; }
    if (!TerminateProcess(p->handle, 1)) { err = "TerminateProcess falló"; return false; }
    return true;
}

std::vector<int> List() {
    std::lock_guard lock(g_mu);
    std::vector<int> ids;
    for (auto& [id, _] : g_procs) ids.push_back(id);
    return ids;
}

} // namespace proc
