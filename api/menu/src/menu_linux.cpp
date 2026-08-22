// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/menu/src/menu_linux.cpp — context menus GTK (popup).
// NOTA: el menú de aplicación es OBLIGATORIO en macOS y OPCIONAL aquí.
// En GNOME/Linux las apps modernas no ponen menubar — v1 expone solo popup;
// el IDE dibuja sus propios menús web si prefiere (no invadimos).
//
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#include <gtk/gtk.h>

namespace menu {

using ow::json::Array;
using ow::json::Object;
using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static const ow_module_host_t* g_host = nullptr;
static uint32_t g_win = 0;

static void OnItemActivate(GtkMenuItem*, gpointer user_data) {
    if (!g_host || !g_host->emit_event) return;
    auto* label = static_cast<std::string*>(user_data);
    std::string json = "{\"label\":" + ow::json::Value(*label).Serialize() +
                       ",\"windowId\":" + std::to_string(g_win) + "}";
    g_host->emit_event(g_host->ctx, g_win, "menu.click", json.c_str());
}

static void BuildItems(GtkMenuShell* shell, const Array& items,
                       std::vector<std::string>& labels) {
    for (const auto& it : items) {
        if (!it.IsObject()) continue;
        std::string type =
            it.Find("type") && (*it.Find("type")).IsString()
                ? (*it.Find("type")).AsString() : "normal";
        if (type == "separator") {
            gtk_menu_shell_append(shell, gtk_separator_menu_item_new());
            continue;
        }
        const Value* labelV = it.Find("label");
        std::string label =
            labelV && labelV->IsString() ? labelV->AsString() : "";
        GtkWidget* w = gtk_menu_item_new_with_label(label.c_str());
        gtk_menu_shell_append(shell, w);

        const Value* sub = it.Find("submenu");
        if (sub && sub->IsArray()) {
            GtkWidget* subm = gtk_menu_new();
            gtk_menu_item_set_submenu(GTK_MENU_ITEM(w), subm);
            BuildItems(GTK_MENU_SHELL(subm), sub->AsArray(), labels);
        } else {
            labels.push_back(label);
            g_signal_connect(w, "activate",
                             G_CALLBACK(+[](GtkMenuItem* m, gpointer ud) {
                                 OnItemActivate(m, ud);
                             }),
                             &labels.back());
        }
    }
}

void popup(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().empty())
        return RespondError(res, "items requeridos");
    g_win = req->window_id;

    GtkMenu* m = GTK_MENU(gtk_menu_new());
    std::vector<std::string> labels;
    BuildItems(GTK_MENU_SHELL(m), parsed.value->AsArray()[0].AsArray(), labels);
    gtk_widget_show_all(GTK_WIDGET(m));
    gtk_menu_popup_at_pointer(m, nullptr); // en el cursor
    // popup es async en GTK3: el menú vive hasta selección
    RespondOk(res, "null");
}

} // namespace menu

extern "C" OW_MODULE_EXPORT void ow_module_set_host(const ow_module_host_t* h) {
    menu::g_host = h;
}

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"popup", &menu::popup},
        {"setApplicationMenu",
         [](const ow_request_t*, ow_response_t* res) {
             // desactivable por diseño: en Linux/GNOME no imponemos menubar
             ow::Module::RespondOk(res, "\"noop\"");
         }},
    };
    static const ow_module_desc_t d{
        "menu", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}
