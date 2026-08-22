// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/dialog/src/dialog_win.cpp — IFileOpenDialog/IFileSaveDialog (COM).
// VERIFICAR-EN-WINDOWS.
//
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

namespace dlg {

using ow::json::Array;
using ow::json::Object;
using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static std::string WideToUtf8(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

void open(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    std::string mode =
        parsed.value && parsed.value->IsArray() && !parsed.value->AsArray().empty() &&
                parsed.value->AsArray()[0].IsString()
            ? parsed.value->AsArray()[0].AsString()
            : "open";

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    // RAII: garantiza CoUninitialize() en cualquier punto de salida de la
    // función (sólo si CoInitializeEx tuvo éxito, incl. S_FALSE).
    struct ComGuard {
        bool active;
        ~ComGuard() { if (active) CoUninitialize(); }
    } comGuard{SUCCEEDED(hr)};
    bool multi = mode == "multi";
    bool save = mode == "save";
    bool dir = mode == "dir";

    IFileDialog* fd = nullptr;
    IID iid = save ? __uuidof(IFileSaveDialog) : __uuidof(IFileOpenDialog);
    if (FAILED(CoCreateInstance(save ? __uuidof(FileSaveDialog) : __uuidof(FileOpenDialog),
                                nullptr, CLSCTX_INPROC_SERVER, iid, (void**)&fd))) {
        return RespondError(res, "COM falló");
    }
    DWORD opts;
    fd->GetOptions(&opts);
    opts |= FOS_FORCEFILESYSTEM | (multi ? FOS_ALLOWMULTISELECT : 0) |
            (dir ? FOS_PICKFOLDERS : 0);
    fd->SetOptions(opts);

    Array results;
    auto finish = [&](bool ok_) {
        fd->Release();
        if (!ok_) RespondOk(res, "null");
    };

    hr = fd->Show(nullptr);
    if (FAILED(hr)) { finish(false); return; }

    if (multi) {
        IFileOpenDialog* fo = nullptr;
        if (SUCCEEDED(fd->QueryInterface(&fo)) && fo) {
            IShellItemArray* items = nullptr;
            if (SUCCEEDED(fo->GetResults(&items)) && items) {
                DWORD count = 0;
                items->GetCount(&count);
                for (DWORD i = 0; i < count; ++i) {
                    IShellItem* it = nullptr;
                    if (SUCCEEDED(items->GetItemAt(i, &it)) && it) {
                        PWSTR p = nullptr;
                        if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                            results.emplace_back(Value(WideToUtf8(p)));
                            CoTaskMemFree(p);
                        }
                        it->Release();
                    }
                }
                items->Release();
            }
            fo->Release();
        }
        finish(true);
        RespondOk(res, Value(std::move(results)).Serialize().c_str());
        return;
    }

    IShellItem* item = nullptr;
    if (SUCCEEDED(fd->GetResult(&item)) && item) {
        PWSTR p = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
            results.emplace_back(Value(WideToUtf8(p)));
            CoTaskMemFree(p);
        }
        item->Release();
    }
    finish(true);
    RespondOk(res, results.empty() ? "null" : results[0].Serialize().c_str());
}

void messageBox(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().size() < 3)
        return RespondError(res, "se esperan [type, title, message]");
    const auto& a = parsed.value->AsArray();

    UINT flags = MB_OK;
    std::string type = a[0].IsString() ? a[0].AsString() : "info";
    if (type == "warning") flags |= MB_ICONWARNING;
    else if (type == "error") flags |= MB_ICONERROR;
    else if (type == "question") { flags |= MB_YESNO | MB_ICONQUESTION; }

    auto toWide = [](const std::string& s) {
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
        return w;
    };

    int r = MessageBoxA(nullptr, a.size() > 2 && a[2].IsString()
                                      ? a[2].AsString().c_str() : "",
                        a.size() > 1 && a[1].IsString() ? a[1].AsString().c_str() : "",
                        flags);
    RespondOk(res, Value(static_cast<int64_t>(r)).Serialize().c_str());
}

} // namespace dlg

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"open", &dlg::open},
        {"messageBox", &dlg::messageBox},
    };
    static const ow_module_desc_t d{
        "dialog", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}
