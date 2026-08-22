// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// VERIFICAR-EN-CI: reemplazar el .exe en ejecución funciona en Windows porque
// el loader abre la imagen con FILE_SHARE_DELETE desde Vista+, lo que permite
// mover/renombrar (no sobreescribir in-place) el archivo mientras corre. El
// proceso viejo sigue vivo con el inode/hardlink original hasta salir; el
// nuevo se lanza a partir del path ya reemplazado.
//
#include "updater_platform.hpp"

#include <filesystem>
#include <windows.h>

namespace upd {

namespace fs = std::filesystem;

std::string CurrentExePath() {
    wchar_t buf[MAX_PATH];
    DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n == MAX_PATH) return {};
    int len = ::WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n), nullptr, 0,
                                    nullptr, nullptr);
    std::string out(static_cast<size_t>(len), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n), out.data(), len,
                          nullptr, nullptr);
    return out;
}

namespace {
std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                                    nullptr, 0);
    std::wstring out(static_cast<size_t>(len), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), len);
    return out;
}
} // namespace

bool ReplaceAndRelaunch(const std::string& newFile, const std::string& exe,
                        std::string& error) {
    std::wstring wNew = Utf8ToWide(newFile);
    std::wstring wExe = Utf8ToWide(exe);

    if (!::MoveFileExW(wNew.c_str(), wExe.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        std::error_code ec;
        fs::copy_file(newFile, exe + ".new", fs::copy_options::overwrite_existing, ec);
        if (ec) { error = "copiado falló"; return false; }
        if (!::MoveFileExW(Utf8ToWide(exe + ".new").c_str(), wExe.c_str(),
                          MOVEFILE_REPLACE_EXISTING)) {
            error = "reemplazo del ejecutable falló (err " +
                    std::to_string(::GetLastError()) + ")";
            return false;
        }
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmdLine = L"\"" + wExe + L"\"";
    if (!::CreateProcessW(wExe.c_str(), cmdLine.data(), nullptr, nullptr, FALSE,
                          0, nullptr, nullptr, &si, &pi)) {
        error = "no se pudo relanzar (err " + std::to_string(::GetLastError()) + ")";
        return false;
    }
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    ::ExitProcess(0); // no retorna
}

} // namespace upd
