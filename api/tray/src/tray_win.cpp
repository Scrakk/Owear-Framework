// VERIFICAR-EN-WINDOWS: Shell_NotifyIconW.
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"
extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_module_desc_t d{"tray", OW_VERSION_STRING, nullptr, 0};
    return &d;
}
