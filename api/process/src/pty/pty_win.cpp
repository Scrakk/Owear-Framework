// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/process/src/pty/pty_win.cpp — ConPTY (Windows 10 1809+).
// VERIFICAR-EN-WINDOWS: requiere kernel32 con CreatePseudoConsole.
//
#include "../registry.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// header dinámico para no requerir SDK nuevo en build:
extern "C" {
HRESULT WINAPI CreatePseudoConsole(COORD size, HANDLE in, HANDLE out, DWORD flags, void** phPC);
HRESULT WINAPI ResizePseudoConsole(void* hPC, COORD size);
void WINAPI ClosePseudoConsole(void* hPC);
}

namespace proc {

namespace {
struct PtyPair { void* pc = nullptr; HANDLE in = nullptr, out = nullptr; };
std::map<int, PtyPair> g_ptyPairs;

std::wstring Utf8(std::string_view s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
} // namespace

long PtyOpen(int* outId, const std::string& cmd, std::vector<std::string> args,
             const std::string& cwd, std::map<std::string, std::string> env,
             int cols, int rows, uint32_t windowId, std::string& err) {
    HANDLE inR = nullptr, inW = nullptr, outR = nullptr, outW = nullptr;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    if (!CreatePipe(&inR, &inW, &sa, 0) || !CreatePipe(&outR, &outW, &sa, 0)) {
        err = "pipes falló"; return -1;
    }

    COORD size{(SHORT)(cols > 0 ? cols : 80), (SHORT)(rows > 0 ? rows : 24)};
    void* pc = nullptr;
    if (FAILED(CreatePseudoConsole(size, inR, outW, 0, &pc))) {
        err = "ConPTY no disponible";
        CloseHandle(inR); CloseHandle(inW); CloseHandle(outR); CloseHandle(outW);
        return -1;
    }
    CloseHandle(inR); CloseHandle(outW);

    std::string cmdline = cmd;
    for (auto& a : args) cmdline += " \"" + a + "\"";

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    // PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE requiere extensiones:
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    auto* attrList = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, attrSize);
    InitializeProcThreadAttributeList(attrList, 1, 0, &attrSize);
    UpdateProcThreadAttribute(attrList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                              pc, sizeof(pc), nullptr, nullptr);
    EXTENDED_STARTUPINFO_STRUCT esi{};
    esi.StartupInfo.cb = sizeof(esi);
    esi.lpAttributeList = attrList;

    BOOL ok = CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, FALSE,
                             EXTENDED_STARTUPINFO_PRESENT, nullptr,
                             cwd.empty() ? nullptr : cwd.c_str(),
                             &esi.StartupInfo, &pi);
    DeleteProcThreadAttributeList(attrList);
    HeapFree(GetProcessHeap(), 0, attrList);
    CloseHandle(inW);
    if (!ok) {
        ClosePseudoConsole(pc);
        CloseHandle(outR);
        err = "CreateProcess ConPTY falló";
        return -1;
    }

    std::lock_guard lock(g_mu);
    int id = g_nextId++;
    auto* p = new Proc();
    p->id = id; p->kind = Kind::Pty;
    p->pid = (pid_t)pi.dwProcessId;
    p->handle = pi.hProcess;
    p->ptyHandle = pc;
    p->masterFd = _open_osfhandle((intptr_t)outR, 0); // lectura
    p->stdinFd = -1;
    // escritura vía inW guardado en mapa auxiliar
    g_ptyPairs[id] = PtyPair{pc, inW, outR};
    p->window_id = windowId;
    *outId = id;
    g_procs[id] = p;

    auto* stopFlag = &p->stop;
    p->reader = std::thread([id, outR, win = windowId, hProc = pi.hProcess, stopFlag] {
        char buf[65 * 1024];
        DWORD n = 0;
        while (!stopFlag->load()) {
            if (!ReadFile(outR, buf, sizeof(buf), &n, nullptr) || n == 0) break;
            EmitData(win, id, "stdout", buf, n);
        }
        CloseHandle(outR);
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

bool PtyResize(int id, int cols, int rows, std::string& err) {
    auto* p = Get(id);
    if (!p || !p->ptyHandle) { err = "pty inválido"; return false; }
    COORD size{(SHORT)cols, (SHORT)rows};
    if (FAILED(ResizePseudoConsole(p->ptyHandle, size))) {
        err = "ResizePseudoConsole falló"; return false;
    }
    return true;
}

} // namespace proc

// write de PTY windows: escribe directo al pipe de entrada del par
bool WritePtyWin(int id, const char* d, size_t l, std::string& e) {
    auto it = g_ptyPairs.find(id);
    if (it == g_ptyPairs.end()) return WriteStdin(id, d, l, e);
    DWORD written = 0;
    if (!WriteFile(it->second.in, d, (DWORD)l, &written, nullptr)) {
        e = "write pty falló"; return false;
    }
    return true;
}
