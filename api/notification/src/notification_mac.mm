// VERIFICAR-EN-WINDOWS: implementación nativa pendiente — módulo presente pero funciones reportan no-soportado.
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"
static const ow_fn_entry_t fns[] = {};
extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_module_desc_t d{"notification", OW_VERSION_STRING, nullptr, 0};
    return &d;
}
