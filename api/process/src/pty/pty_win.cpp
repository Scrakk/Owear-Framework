// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/process/src/pty/pty_win.cpp — ConPTY (Windows 10 1809+).
//
// Las funciones ConPTY se cargan DINÁMICAMENTE desde kernel32: compila con
// cualquier SDK (incl. MinGW viejo sin cabeceras de Win10) y falla con error
// claro en runtime si el SO no tiene ConPTY.
//
#include "../registry.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <io.h>   // _open_osfhandle

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

namespace proc {

namespace {

// ── API ConPTY cargada en runtime ───────────────────────────────────────────
struct ConApi {
    HRESULT (WINAPI *create)(COORD, HANDLE, HANDLE, DWORD, void**) = nullptr;
    HRESULT (WINAPI *resize)(void*, COORD) = nullptr;
    void (WINAPI *close)(void*) = nullptr;
    bool ok = false;
};

ConApi LoadConApi() {
    ConApi a;
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (!k32) return a;
    a.create = reinterpret_cast<decltype(a.create)>(
        GetProcAddress(k32, "CreatePseudoConsole"));
    a.resize = reinterpret_cast<decltype(a.resize)>(
        GetProcAddress(k32, "ResizePseudoConsole"));
    a.close = reinterpret_cast<decltype(a.close)>(
        GetProcAddress(k32, "ClosePseudoConsole"));
    a.ok = a.create && a.resize && a.close;
    return a;
}

const ConApi k_con = LoadConApi();

struct PtyPair {
    void* pc = nullptr;
    HANDLE in = nullptr;   // escritura al stdin del PTY
    HANDLE out = nullptr;  // lectura del stdout del PTY
};
std::map<int, PtyPair> g_ptyPairs;
std::mutex g_ptyPairsMu;

void ClosePtyPair(int id) {
    std::lock_guard lock(g_ptyPairsMu);
    auto it = g_ptyPairs.find(id);
    if (it == g_ptyPairs.end()) return;
    if (it->second.pc && k_con.close) k_con.close(it->second.pc);
    if (it->second.in) CloseHandle(it->second.in);
    g_ptyPairs.erase(it);
}

std::wstring Utf8(std::string_view s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        w.data(), n);
    return w;
}

/// Hilo lector del PTY: stdout → eventos + espera exit + cleanup del par.
void PtyReader(int id, HANDLE outR, uint32_t win, HANDLE hProc,
               const std::atomic<bool>* stop) {
    char buf[65 * 1024];
    DWORD n = 0;
    while (!stop->load()) {
        if (!ReadFile(outR, buf, sizeof(buf), &n, nullptr) || n == 0) break;
        EmitData(win, id, "stdout", buf, n);
    }
    CloseHandle(outR);
    WaitForSingleObject(hProc, INFINITE);
    DWORD code = static_cast<DWORD>(-1);
    GetExitCodeProcess(hProc, &code);
    CloseHandle(hProc);
    Emit(win, "process.exit",
         "{\"procId\":" + std::to_string(id) + ",\"code\":" +
             std::to_string(static_cast<int>(code)) + ",\"signal\":0}");
    ClosePtyPair(id);
    Remove(id);
}

} // namespace

long PtyOpen(int* outId, const std::string& cmd, std::vector<std::string> args,
             const std::string& cwd, std::map<std::string, std::string> env,
             int cols, int rows, uint32_t windowId, std::string& err) {
    if (!k_con.ok) {
        err = "ConPTY no disponible en este Windows (<10 1809)";
        return -1;
    }

    HANDLE inR = nullptr, inW = nullptr, outR = nullptr, outW = nullptr;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    if (!CreatePipe(&inR, &inW, &sa, 0) || !CreatePipe(&outR, &outW, &sa, 0)) {
        err = "pipes falló";
        return -1;
    }

    COORD size{static_cast<SHORT>(cols > 0 ? cols : 80),
               static_cast<SHORT>(rows > 0 ? rows : 24)};
    void* pc = nullptr;
    if (FAILED(k_con.create(size, inR, outW, 0, &pc))) {
        err = "ConPTY no disponible";
        CloseHandle(inR); CloseHandle(inW);
        CloseHandle(outR); CloseHandle(outW);
        return -1;
    }
    CloseHandle(inR);  // el PTY tiene su extremo de lectura
    CloseHandle(outW); // y su extremo de escritura

    std::string cmdline = cmd;
    for (auto& a : args) cmdline += " \"" + a + "\"";

    STARTUPINFOEXA esi{};
    esi.StartupInfo.cb = sizeof(esi);
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    auto* attrList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
        HeapAlloc(GetProcessHeap(), 0, attrSize));
    if (!attrList ||
        !InitializeProcThreadAttributeList(attrList, 1, 0, &attrSize) ||
        !UpdateProcThreadAttribute(attrList, 0,
                                   PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   pc, sizeof(pc), nullptr, nullptr)) {
        err = "attribute list falló";
        if (attrList) {
            DeleteProcThreadAttributeList(attrList);
            HeapFree(GetProcessHeap(), 0, attrList);
        }
        k_con.close(pc);
        CloseHandle(inW); CloseHandle(outR);
        return -1;
    }
    esi.lpAttributeList = attrList;

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, FALSE,
                             EXTENDED_STARTUPINFO_PRESENT, nullptr,
                             cwd.empty() ? nullptr : cwd.c_str(),
                             &esi.StartupInfo, &pi);
    DeleteProcThreadAttributeList(attrList);
    HeapFree(GetProcessHeap(), 0, attrList);
    CloseHandle(inW);
    if (!ok) {
        k_con.close(pc);
        CloseHandle(outR);
        err = "CreateProcess ConPTY falló";
        return -1;
    }

    int id = 0;
    {
        std::lock_guard lock(g_mu);
        id = g_nextId++;
        auto* p = new Proc();
        p->id = id;
        p->kind = Kind::Pty;
        p->pid = static_cast<pid_t>(pi.dwProcessId);
        p->handle = pi.hProcess;
        p->ptyHandle = pc;
        p->masterFd = _open_osfhandle(reinterpret_cast<intptr_t>(outR), 0);
        p->stdinFd = -1;
        p->window_id = windowId;
        *outId = id;
        g_procs[id] = p;
        {
            std::lock_guard pl(g_ptyPairsMu);
            g_ptyPairs[id] = PtyPair{pc, inW, outR};
        }
    }

    auto* stopFlag = &g_procs[id]->stop;
    PtyReader(id, outR, windowId, pi.hProcess, stopFlag);
    return static_cast<long>(pi.dwProcessId);
}

bool PtyResize(int id, int cols, int rows, std::string& err) {
    auto* p = Get(id);
    if (!p || !p->ptyHandle) { err = "pty inválido"; return false; }
    COORD size{static_cast<SHORT>(cols), static_cast<SHORT>(rows)};
    if (FAILED(k_con.resize(p->ptyHandle, size))) {
        err = "ResizePseudoConsole falló";
        return false;
    }
    return true;
}

bool WritePtyWin(int id, const char* data, size_t len, std::string& err) {
    HANDLE in = nullptr;
    {
        std::lock_guard pl(g_ptyPairsMu);
        auto it = g_ptyPairs.find(id);
        if (it == g_ptyPairs.end()) { err = "pty inválido"; return false; }
        in = it->second.in;
    }
    DWORD written = 0;
    if (!WriteFile(in, data, static_cast<DWORD>(len), &written, nullptr) ||
        written != len) {
        err = "escritura al PTY falló";
        return false;
    }
    return true;
}

} // namespace proc
