# ── Módulos stock de Owear ───────────────────────────────────────────────────
# Cada módulo: target MODULE (shared lib) exportando ow_module_descriptor.
# El kernel los consume igual que cualquier .owm de usuario.

set(OW_MODULE_CFLAGS -DOW_MODULE_BUILD)

add_library(owear-module-fs MODULE fs/fs_module.cpp)
target_include_directories(owear-module-fs PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include)
target_compile_options(owear-module-fs PRIVATE ${OW_MODULE_CFLAGS} -Wall)
set_target_properties(owear-module-fs PROPERTIES PREFIX "" OUTPUT_NAME "fs")

if(OW_PLATFORM STREQUAL "linux")
    install(TARGETS owear-module-fs LIBRARY DESTINATION modules)
endif()
