// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/app/src/app_builtin.cpp — builtin "app".
// setBadgeCount · requestSingleInstanceLock (socket probe + argv handoff) ·
// relaunch.
//
#include "../../../src/Core/BuiltinUtil.hpp"
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#include <gio/gio.h>
#include <glib-unix.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <glib-unix.h>

#include <cstdlib>
#include <cstring>
#include <unistd.h>

namespace appmod {

using ow::json::Array;
using ow::json::Object;
using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static std::string AppId() {
    const char* id = std::getenv("OW_APP_ID");
    return (id && *id) ? id : "default";
}

static std::string LockPath() {
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    std::string dir = (xdg && *xdg) ? xdg : "/tmp";
    char buf[300];
    snprintf(buf, sizeof(buf), "%s/owear-single-%s.sock", dir.c_str(),
             AppId().c_str());
    return buf;
}

// ── badge (Unity LauncherEntry; otros escritorios → error claro) ────────────
void setBadgeCount(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() ||
        !parsed.value->AsArray()[0].IsNumber())
        return RespondError(res, "count requerido");
    int64_t count = parsed.value->AsArray()[0].AsInt();

    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    if (!bus) return RespondError(res, "sin sesión D-Bus");

    // desktop file esperado: <appId>.desktop
    std::string uri = "application://" + AppId() + ".desktop";
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("(sa{sv})"));
    g_variant_builder_add(&b, "s", uri.c_str());
    GVariantBuilder props;
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&props, "{sv}", "count", g_variant_new_int64(count));
    g_variant_builder_add(&props, "{sv}", "count-visible",
                          g_variant_new_boolean(count > 0));
    g_variant_builder_add(&b, "a{sv}", &props);

    GError* e = nullptr;
    g_dbus_connection_call_sync(bus, nullptr, // unique name del launcher
                                "/com/canonical/Unity/LauncherEntry",
                                "com.canonical.Unity.LauncherEntry", "Update",
                                g_variant_builder_end(&b), nullptr,
                                G_DBUS_CALL_FLAGS_NONE, 500, nullptr, &e);
    g_object_unref(bus);
    if (e) {
        std::string err = e->message;
        g_error_free(e);
        return RespondError(res,
                            "badge no soportado en este escritorio: " + err);
    }
    RespondOk(res, "null");
}

// ── single instance ─────────────────────────────────────────────────────────
// Primera instancia: bind+listen del socket de lock. Segunda: conecta, envía
// su argv y sale → la primera emite evento "secondInstance".
static int g_lockFd = -1;

void requestSingleInstanceLock(const ow_request_t* req, ow_response_t* res) {
    std::string path = LockPath();
    ::unlink(path.c_str()); // socket huérfano de un crash previo

    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        // ya existe una instancia → le pasa nuestro argv y sale
        int cfd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (cfd >= 0 && ::connect(cfd, reinterpret_cast<sockaddr*>(&addr),
                                  sizeof(addr)) == 0) {
            // lee argv de /proc/self/cmdline (separado por \0)
            Array arr;
            FILE* f = fopen("/proc/self/cmdline", "rb");
            if (f) {
                char line[2048];
                size_t n;
                while ((n = fread(line, 1, sizeof(line), f)) > 0) {
                    size_t off = 0;
                    while (off < n) {
                        std::string arg(line + off);
                        if (arg.empty()) { ++off; continue; }
                        arr.emplace_back(Value(arg));
                        off += arg.size() + 1;
                    }
                    break; // cmdline cabe en un read
                }
                fclose(f);
            }
            Object payload;
            payload.emplace_back("argv", Value(std::move(arr)));
            std::string json = Value(std::move(payload)).Serialize();
            size_t off = 0;
            while (off < json.size()) {
                ssize_t w = ::write(cfd, json.data() + off, json.size() - off);
                if (w <= 0) break;
                off += static_cast<size_t>(w);
            }
            close(cfd);
        }
        RespondOk(res, "false");
        return;
    }
    ::listen(fd, 4);
    g_lockFd = fd;

    // acepta conexiones en el main loop vía GSource
    GMainContext* ctx = g_main_context_default();
    GSource* src = g_unix_fd_source_new(fd, G_IO_IN);
    auto* fdHeap = new int(fd);
    g_source_set_callback(
        src,
        +[](gpointer ud) -> gboolean {
            int fd2 = *static_cast<int*>(ud);
            int cfd = ::accept(fd2, nullptr, nullptr);
            if (cfd >= 0) {
                char buf[8192] = {};
                ssize_t n = ::read(cfd, buf, sizeof(buf) - 1);
                if (n > 0)
                    ow::builtin::Emit(
                        0, "secondInstance",
                        std::string(buf, static_cast<size_t>(n)));
                close(cfd);
            }
            return TRUE;
        },
        fdHeap,
        +[](gpointer ud) { delete static_cast<int*>(ud); });
    g_source_attach(src, ctx);

    RespondOk(res, "true");
}

// relanza el propio binario con el mismo argv (execv)
void relaunch(const ow_request_t*, ow_response_t* res) {
    char exe[4096] = {};
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0) return RespondError(res, "no se resolvió el exe");
    exe[n] = 0;

    // reconstruye argv desde /proc/self/cmdline
    static char argvBuf[8192];
    int argc = 0;
    char* argv[128] = {};
    FILE* f = fopen("/proc/self/cmdline", "rb");
    if (f) {
        size_t total = fread(argvBuf, 1, sizeof(argvBuf) - 1, f);
        fclose(f);
        size_t off = 0;
        while (off < total && argc < 127) {
            argv[argc++] = argvBuf + off;
            off += strlen(argvBuf + off) + 1;
        }
    }
    argv[argc] = nullptr;

    RespondOk(res, "null");
    execv(exe, argv);
}

} // namespace appmod

namespace ow::internal {
const ow_module_desc_t* AppModuleDescriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"setBadgeCount", &appmod::setBadgeCount},
        {"requestSingleInstanceLock", &appmod::requestSingleInstanceLock},
        {"relaunch", &appmod::relaunch},
    };
    static const ow_module_desc_t d{
        "app", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;

} // namespace ow::internal
} // namespace appmod
