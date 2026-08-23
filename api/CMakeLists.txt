# ── Todas las APIs de Owear ──────────────────────────────────────────────────
# Cada folder api/<nombre>/ es una API independiente con sus propios módulos
# y target .owm propio (actualización modular). Las APIs acopladas al WebView
# (session, window, capturer) compilan como builtins dentro del kernel, pero
# su código vive aquí igualmente.

set(OW_API_COMMON_FLAGS -DOW_MODULE_BUILD)

# Extensión de los archivos fuente específicos de plataforma: macOS usa
# Objective-C++ (.mm) para hablar con Cocoa/AppKit; Linux/Windows usan .cpp.
# Los módulos seleccionan su fuente como src/<archivo>_${OW_PLATFORM}.${OW_SRC_EXT}
if(OW_PLATFORM STREQUAL "mac")
    set(OW_SRC_EXT "mm")
else()
    set(OW_SRC_EXT "cpp")
endif()

# Dependencias GUI compartidas por varias APIs (dialog, clipboard, screen,
# menu, notification, power, shell) — GTK trae gio/gobject.
if(OW_PLATFORM STREQUAL "linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(API_GTK REQUIRED gtk+-3.0)
    set(OW_GUI_INCS ${API_GTK_INCLUDE_DIRS})
    set(OW_GUI_LIBS ${API_GTK_LIBRARIES})
endif()

function(ow_add_module NAME)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS;INCLUDES" ${ARGN})
    add_library(owear-api-${NAME} MODULE ${ARG_SOURCES})
    target_include_directories(owear-api-${NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${ARG_INCLUDES})
    if(MSVC)
        target_compile_options(owear-api-${NAME} PRIVATE
            ${OW_API_COMMON_FLAGS} /W4)
    else()
        target_compile_options(owear-api-${NAME} PRIVATE
            ${OW_API_COMMON_FLAGS} -Wall -Wextra)
    endif()
    target_link_libraries(owear-api-${NAME} PRIVATE ${ARG_LIBS})
    if(MSVC)
        # Los .owm importan ow_shm_* (dllimport): enlazan contra la import
        # library que MSVC genera para el exe del kernel (ENABLE_EXPORTS).
        target_link_libraries(owear-api-${NAME} PRIVATE owear)
    endif()
    if(APPLE)
        # Los símbolos del host (ow_shm_*) se resuelven al hacer dlopen:
        # patrón plugin estándar en Mach-O (equivalente a -rdynamic en Linux).
        target_link_options(owear-api-${NAME} PRIVATE -Wl,-undefined,dynamic_lookup)
    endif()
    set_target_properties(owear-api-${NAME} PROPERTIES
        PREFIX "" OUTPUT_NAME "${NAME}")
endfunction()

# ── fs ────────────────────────────────────────────────────────────────────────
add_subdirectory(fs)

# ── path (lógica pura + dirs por plataforma) ─────────────────────────────────
add_subdirectory(path)

# ── process (spawn + PTY manager) ─────────────────────────────────────────────
add_subdirectory(process)

# ── dialog ────────────────────────────────────────────────────────────────────
add_subdirectory(dialog)

# ── clipboard ─────────────────────────────────────────────────────────────────
add_subdirectory(clipboard)

# ── shell ─────────────────────────────────────────────────────────────────────
add_subdirectory(shell)

# ── screen ────────────────────────────────────────────────────────────────────
add_subdirectory(screen)

# ── menu ──────────────────────────────────────────────────────────────────────
add_subdirectory(menu)

# ── notification ──────────────────────────────────────────────────────────────
add_subdirectory(notification)

# ── globalshortcut ────────────────────────────────────────────────────────────
add_subdirectory(globalshortcut)

# ── tray ──────────────────────────────────────────────────────────────────────
add_subdirectory(tray)

# ── net (HTTP nativo sin CORS) ────────────────────────────────────────────────
add_subdirectory(net)

# ── power (powerMonitor + powerSaveBlocker) ──────────────────────────────────
add_subdirectory(power)

# ── updater (autoUpdater genérico manifest+sha256+replace) ───────────────────
add_subdirectory(updater)

# ── capturer (captura de pantallas X11 → PNG → SHM) ──────────────────────────
add_subdirectory(capturer)
