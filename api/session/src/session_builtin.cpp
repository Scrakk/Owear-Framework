// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/session/src/session_builtin.cpp — builtin "session".
// Cookies · caché/datos de sitio · proxy · downloads con eventos.
//
// Corre en el main thread GTK. Puentes async→sync con GMainLoop anidado
// (mismo patrón que los diálogos modales).
//
#include "../../../src/Core/BuiltinUtil.hpp"
#include "../../../src/Control/ControlServer.hpp"
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#include <gtk/gtk.h>
#include <libsoup/soup.h>
#include <webkit2/webkit2.h>

#include <cstring>
#include <map>

namespace sess {

using ow::json::Array;
using ow::json::Object;
using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static WebKitCookieManager* CookieMgr() {
    return webkit_web_context_get_cookie_manager(
        webkit_web_context_get_default());
}

static uint32_t WinId(const Value& args) {
    if (!args.IsArray() || args.AsArray().empty() || !args.AsArray()[0].IsNumber())
        return 0;
    return static_cast<uint32_t>(args.AsArray()[0].AsInt());
}

static std::string CookieListToJson(GList* list, const char* filterName,
                                    const char* filterDomain) {
    Array arr;
    for (GList* l = list; l; l = l->next) {
        auto* ck = static_cast<SoupCookie*>(l->data);
        if (filterName && *filterName &&
            strcmp(soup_cookie_get_name(ck), filterName) != 0)
            continue;
        if (filterDomain && *filterDomain &&
            strstr(soup_cookie_get_domain(ck), filterDomain) == nullptr)
            continue;
        Object o;
        o.emplace_back("name", Value(std::string(soup_cookie_get_name(ck))));
        o.emplace_back("value", Value(std::string(soup_cookie_get_value(ck))));
        o.emplace_back("domain", Value(std::string(soup_cookie_get_domain(ck))));
        o.emplace_back("path", Value(std::string(soup_cookie_get_path(ck))));
        o.emplace_back("httpOnly", Value(soup_cookie_get_http_only(ck) != FALSE));
        o.emplace_back("secure", Value(soup_cookie_get_secure(ck) != FALSE));
        arr.push_back(Value(std::move(o)));
    }
    Object out;
    out.emplace_back("cookies", Value(std::move(arr)));
    return Value(std::move(out)).Serialize();
}

// ── cookies ────────────────────────────────────────────────────────────────

// args: [url, name?, domain?]
void cookiesGet(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() ||
        parsed.value->AsArray().empty() || !parsed.value->AsArray()[0].IsString())
        return RespondError(res, "url requerida");
    const auto& a = parsed.value->AsArray();
    std::string url = a[0].AsString();
    std::string name = a.size() > 1 && a[1].IsString() ? a[1].AsString() : "";
    std::string domain = a.size() > 2 && a[2].IsString() ? a[2].AsString() : "";

    struct Ctx {
        GMainLoop* loop = nullptr;
        GList* list = nullptr;
        std::string error;
    } ctx;
    ctx.loop = g_main_loop_new(nullptr, FALSE);

    webkit_cookie_manager_get_cookies(
        CookieMgr(), url.c_str(), nullptr,
        [](GObject* obj, GAsyncResult* r, gpointer ud) {
            auto* c = static_cast<Ctx*>(ud);
            GError* e = nullptr;
            c->list = webkit_cookie_manager_get_cookies_finish(
                WEBKIT_COOKIE_MANAGER(obj), r, &e);
            if (e) {
                c->error = e->message;
                g_error_free(e);
            }
            g_main_loop_quit(c->loop);
        },
        &ctx);
    g_main_loop_run(ctx.loop);
    g_main_loop_unref(ctx.loop);

    if (!ctx.error.empty()) {
        if (ctx.list)
            g_list_free_full(ctx.list,
                             reinterpret_cast<GDestroyNotify>(soup_cookie_free));
        return RespondError(res, ctx.error);
    }
    std::string json = CookieListToJson(ctx.list, name.c_str(), domain.c_str());
    if (ctx.list)
        g_list_free_full(ctx.list,
                         reinterpret_cast<GDestroyNotify>(soup_cookie_free));
    RespondOk(res, json.c_str());
}

// args: [{name, value, domain, path?, maxAge?}]
void cookiesSet(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() ||
        !parsed.value->AsArray()[0].IsObject())
        return RespondError(res, "objeto cookie requerido");
    const auto& c = parsed.value->AsArray()[0];

    auto gs = [&](const char* k) -> std::string {
        const Value* v = c.Find(k);
        return v && v->IsString() ? v->AsString() : "";
    };
    std::string name = gs("name"), value = gs("value"), domain = gs("domain");
    int maxAge = -1;
    if (const Value* v = c.Find("maxAge"); v && v->IsNumber())
        maxAge = static_cast<int>(v->AsInt());
    if (name.empty() || domain.empty())
        return RespondError(res, "name y domain requeridos");

    SoupCookie* ck = soup_cookie_new(name.c_str(), value.c_str(), domain.c_str(),
                                     "/", maxAge);
    webkit_cookie_manager_add_cookie(CookieMgr(), ck, nullptr, nullptr, nullptr);
    soup_cookie_free(ck);
    RespondOk(res, "null");
}

// args: [url, name]
void cookieDelete(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().size() < 2)
        return RespondError(res, "se esperan [url, name]");
    const auto& a = parsed.value->AsArray();
    std::string url = a[0].IsString() ? a[0].AsString() : "";
    std::string target = a[1].IsString() ? a[1].AsString() : "";

    struct Ctx {
        GMainLoop* loop = nullptr;
        GList* list = nullptr;
    } ctx;
    ctx.loop = g_main_loop_new(nullptr, FALSE);

    webkit_cookie_manager_get_cookies(
        CookieMgr(), url.c_str(), nullptr,
        [](GObject* obj, GAsyncResult* r, gpointer ud) {
            auto* c = static_cast<Ctx*>(ud);
            c->list = webkit_cookie_manager_get_cookies_finish(
                WEBKIT_COOKIE_MANAGER(obj), r, nullptr);
            g_main_loop_quit(c->loop);
        },
        &ctx);
    g_main_loop_run(ctx.loop);

    bool found = false;
    for (GList* l = ctx.list; l; l = l->next) {
        auto* ck = static_cast<SoupCookie*>(l->data);
        if (strcmp(soup_cookie_get_name(ck), target.c_str()) == 0) {
            webkit_cookie_manager_delete_cookie(CookieMgr(), ck, nullptr, nullptr,
                                                nullptr);
            found = true;
        }
    }
    if (ctx.list)
        g_list_free_full(ctx.list,
                         reinterpret_cast<GDestroyNotify>(soup_cookie_free));
    RespondOk(res, found ? "true" : "false");
}

// ── caché / datos de sitio ────────────────────────────────────────────────

// args: [windowId, {cookies?, localStorage?}?]
void clearStorage(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().empty())
        return RespondError(res, "windowId requerido");
    void* view = ow::builtin::WebviewById(WinId(*parsed.value));
    if (!view) return RespondError(res, "ventana no encontrada");

    WebKitWebContext* wctx =
        webkit_web_view_get_context(WEBKIT_WEB_VIEW(view));

    guint types = WEBKIT_WEBSITE_DATA_DISK_CACHE | WEBKIT_WEBSITE_DATA_MEMORY_CACHE;
    if (parsed.value->AsArray().size() > 1 &&
        parsed.value->AsArray()[1].IsObject()) {
        const auto& opts = parsed.value->AsArray()[1];
        if (auto* v = opts.Find("cookies"); v && v->IsBool() && v->AsBool())
            types |= WEBKIT_WEBSITE_DATA_COOKIES;
        if (auto* v = opts.Find("localStorage"); v && v->IsBool() && v->AsBool())
            types |= WEBKIT_WEBSITE_DATA_LOCAL_STORAGE;
    }

    WebKitWebsiteDataManager* mgr = webkit_web_context_get_website_data_manager(wctx);
    webkit_website_data_manager_clear(mgr,
                                      static_cast<WebKitWebsiteDataTypes>(types),
                                      0 /*todo el histórico*/, nullptr, nullptr,
                                      nullptr);
    webkit_web_context_clear_cache(wctx);
    RespondOk(res, "null");
}

// args: [windowId, proxyUrl | "system"]
void setProxy(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().size() < 2)
        return RespondError(res, "se esperan [windowId, proxyUrl]");
    const auto& a = parsed.value->AsArray();
    void* view = ow::builtin::WebviewById(static_cast<uint32_t>(a[0].AsInt()));
    if (!view) return RespondError(res, "ventana no encontrada");

    WebKitWebContext* wctx = webkit_web_view_get_context(WEBKIT_WEB_VIEW(view));
    std::string proxyUrl = a[1].IsString() ? a[1].AsString() : "system";
    if (proxyUrl == "system") {
        webkit_web_context_set_network_proxy_settings(
            wctx, WEBKIT_NETWORK_PROXY_MODE_DEFAULT, nullptr);
    } else {
        WebKitNetworkProxySettings* s =
            webkit_network_proxy_settings_new(proxyUrl.c_str(), nullptr);
        webkit_web_context_set_network_proxy_settings(
            wctx, WEBKIT_NETWORK_PROXY_MODE_CUSTOM, s);
        webkit_network_proxy_settings_free(s);
    }
    RespondOk(res, "null");
}

// ── downloads ─────────────────────────────────────────────────────────────

struct DlInfo {
    uint32_t window_id = 0;
    WebKitDownload* dl = nullptr;
};
static std::map<int, DlInfo> g_downloads;
static int g_nextDl = 1;

static void OnDlProgress(GObject* obj, GParamSpec*, gpointer ud) {
    int id = GPOINTER_TO_INT(ud);
    auto it = g_downloads.find(id);
    if (it == g_downloads.end()) return;
    gdouble progress = 0;
    g_object_get(obj, "estimated-progress", &progress, nullptr);
    Object o;
    o.emplace_back("downloadId", Value(static_cast<int64_t>(id)));
    o.emplace_back("progress", Value(progress));
    ow::builtin::Emit(it->second.window_id, "session.downloadProgress",
                  Value(std::move(o)).Serialize());
}

static void OnDlFinished(WebKitDownload* dl, gpointer ud) {
    int id = GPOINTER_TO_INT(ud);
    auto it = g_downloads.find(id);
    if (it == g_downloads.end()) return;
    Object o;
    o.emplace_back("downloadId", Value(static_cast<int64_t>(id)));
    ow::builtin::Emit(it->second.window_id, "session.downloadFinished",
                  Value(std::move(o)).Serialize());
    g_object_unref(it->second.dl);
    g_downloads.erase(id);
}

static void OnDownloadStarted(WebKitWebView* /*view*/, WebKitDownload* dl,
                              gpointer user_wid) {
    int id = g_nextDl++;
    auto wid = static_cast<uint32_t>(reinterpret_cast<guintptr>(user_wid));
    g_downloads[id] = DlInfo{wid, static_cast<WebKitDownload*>(g_object_ref(dl))};

    const gchar* uri = webkit_download_get_destination(dl);
    Object o;
    o.emplace_back("downloadId", Value(static_cast<int64_t>(id)));
    o.emplace_back("destination", Value(std::string(uri ? uri : "")));
    ow::builtin::Emit(wid, "session.downloadStarted", Value(std::move(o)).Serialize());

    g_signal_connect(dl, "notify::estimated-progress",
                     G_CALLBACK(+[](GObject* o2, GParamSpec* p, gpointer u) {
                         OnDlProgress(o2, p, u);
                     }),
                     GINT_TO_POINTER(id));
    g_signal_connect(dl, "finished", G_CALLBACK(+[](WebKitDownload* d2, gpointer u) {
                         OnDlFinished(d2, u);
                     }),
                     GINT_TO_POINTER(id));
}

// args: [windowId] — conecta el seguimiento de descargas de esa ventana
void attach(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().empty())
        return RespondError(res, "windowId requerido");
    uint32_t id = WinId(*parsed.value);
    void* view = ow::builtin::WebviewById(id);
    if (!view) return RespondError(res, "ventana no encontrada");

    g_object_set_data(G_OBJECT(view), "ow-win-id",
                      reinterpret_cast<gpointer>(static_cast<guintptr>(id)));

    // 'download-started' vive en el WebContext, no en el view
    WebKitWebContext* wctx = webkit_web_view_get_context(WEBKIT_WEB_VIEW(view));
    if (!g_object_get_data(G_OBJECT(wctx), "ow-dl-connected")) {
        g_object_set_data(G_OBJECT(wctx), "ow-dl-connected", GINT_TO_POINTER(1));
        g_signal_connect(wctx, "download-started",
                         G_CALLBACK(+[](WebKitWebContext*, WebKitDownload* dl,
                                        gpointer) {
                             // windowId: usa la ventana más reciente con attach
                             uint32_t wid = 0;
                             if (!ow::LiveWindows().empty())
                                 wid = ow::LiveWindows().rbegin()->first;
                             OnDownloadStarted(nullptr, dl,
                                               reinterpret_cast<gpointer>(
                                                   static_cast<guintptr>(wid)));
                         }),
                         nullptr);
    }
    RespondOk(res, "null");
}

// args: [downloadId]
void downloadCancel(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().empty())
        return RespondError(res, "downloadId requerido");
    int id = static_cast<int>(parsed.value->AsArray()[0].AsInt());
    auto it = g_downloads.find(id);
    if (it == g_downloads.end()) return RespondError(res, "descarga inexistente");
    webkit_download_cancel(it->second.dl);
    RespondOk(res, "null");
}


// ── extras F-next ──────────────────────────────────────────────────────────

// args: [ua] — aplica a TODAS las ventanas vivas
void setUserAgentAll(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || !parsed.value->AsArray()[0].IsString())
        return RespondError(res, "userAgent requerido");
    std::string ua = parsed.value->AsArray()[0].AsString();
    for (auto& [id, w] : ow::LiveWindows()) {
        auto* view = static_cast<WebKitWebView*>(ow::builtin::WebviewById(id));
        if (!view) continue;
        webkit_settings_set_user_agent(webkit_web_view_get_settings(view),
                                       ua.c_str());
    }
    RespondOk(res, "null");
}

// args: [windowId, langs...] ej: ["es","en"] · vacío = off
void spellCheck(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().empty())
        return RespondError(res, "se esperan [windowId, lang1, lang2…]");
    const auto& a = parsed.value->AsArray();
    void* view = ow::builtin::WebviewById(static_cast<uint32_t>(a[0].AsInt()));
    if (!view) return RespondError(res, "ventana no encontrada");
    WebKitWebContext* wctx =
        webkit_web_view_get_context(WEBKIT_WEB_VIEW(view));

    if (a.size() > 1) {
        const gchar* langs[8] = {};
        guint n = 0;
        std::string keep[8];
        for (guint i = 1; i < a.size() && n < 7; ++i) {
            if (!a[i].IsString()) continue;
            keep[n] = a[i].AsString(); // los settings referencian los strings
            langs[n++] = keep[n].c_str();
        }
        langs[n] = nullptr;
        webkit_web_context_set_spell_checking_enabled(wctx, TRUE);
        webkit_web_context_set_spell_checking_languages(wctx, langs);
    } else {
        webkit_web_context_set_spell_checking_enabled(wctx, FALSE);
    }
    RespondOk(res, "null");
}

} // namespace sess

namespace ow::internal {
const ow_module_desc_t* SessionDescriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"cookiesGet", &sess::cookiesGet},
        {"cookiesSet", &sess::cookiesSet},
        {"cookieDelete", &sess::cookieDelete},
        {"clearStorage", &sess::clearStorage},
        {"setProxy", &sess::setProxy},
        {"attach", &sess::attach},
        {"downloadCancel", &sess::downloadCancel},
        {"setUserAgentAll", &sess::setUserAgentAll},
        {"spellCheck", &sess::spellCheck},
    };
    static const ow_module_desc_t d{
        "session", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}
} // namespace ow::internal
