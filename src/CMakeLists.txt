# ── Owear kernel + plataforma ────────────────────────────────────────────────
# Selección de fuentes POR PLATAFORMA vía CMake (nunca #ifdef de features).
# Cambiar los contratos en include/ow/ rompe las 3 plataformas aquí, en compile-time.

find_package(Threads REQUIRED)

if(OW_PLATFORM STREQUAL "linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    pkg_check_modules(WEBKIT REQUIRED webkit2gtk-4.1)
    pkg_check_modules(OPENSSL REQUIRED openssl)
    pkg_check_modules(GIO_UNIX REQUIRED gio-unix-2.0)
elseif(OW_PLATFORM STREQUAL "mac")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(OPENSSL REQUIRED openssl)
    pkg_check_modules(GLIB2 REQUIRED glib-2.0 gio-2.0)
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(WEBKIT_FRAMEWORK WebKit REQUIRED)
endif()

find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

set(OW_KERNEL_SOURCES
    Core/App.cpp
    Core/Log.cpp
    Core/ModuleLoader.cpp
    Core/WindowModule.cpp
    Bridge/Codec.cpp
    Bridge/BridgeScript.cpp
    Bridge/Dispatcher.cpp
    Bridge/Shm.cpp
    Control/ControlServer.cpp
    Window/Window_common.cpp
    Runtime/Sha256.cpp
    Runtime/TarGz.cpp
    Runtime/Http.cpp
    Runtime/NodeManager.cpp
)

# window-extras/session/crashreporter dependen de GTK+WebKitGTK/libsoup o de
# APIs POSIX (execinfo.h) que hoy solo tienen implementación real en Linux.
# Se compilaban incondicionalmente y rompían la configuración en Windows/macOS
# (headers inexistentes en esas plataformas). Gateados aquí hasta que existan
# implementaciones _mac/_win reales — ver docs/ROADMAP.md.
# Builtins que requieren GLib/GTK (solo Linux hasta tener impl nativa win/mac).
if(OW_PLATFORM STREQUAL "linux")
    list(APPEND OW_KERNEL_SOURCES
        ${CMAKE_SOURCE_DIR}/api/app/src/app_builtin.cpp
        ${CMAKE_SOURCE_DIR}/api/window/src/window_extra.cpp
        ${CMAKE_SOURCE_DIR}/api/session/src/session_builtin.cpp
        ${CMAKE_SOURCE_DIR}/api/crashreporter/src/crashreporter.cpp
    )
endif()

if(OW_PLATFORM STREQUAL "linux")
    list(APPEND OW_KERNEL_SOURCES
        Core/App_linux.cpp
        Window/window_linux.cpp
        Webview/linux/WebKitGTKBackend.cpp
        Webview/linux/BuiltinUtil_linux.cpp
        Control/ControlServer_linux.cpp
        Runtime/NodeManager_linux.cpp
    )
elseif(OW_PLATFORM STREQUAL "win")
    list(APPEND OW_KERNEL_SOURCES
        Core/App_win.cpp
        Window/window_win.cpp
        Webview/win/Webview2Backend.cpp
        Control/ControlServer_win.cpp
        Runtime/NodeManager_win.cpp
    )
elseif(OW_PLATFORM STREQUAL "mac")
    list(APPEND OW_KERNEL_SOURCES
        Core/App_mac.mm
        Window/window_mac.mm
        Webview/mac/WKWebviewBackend.mm
        Control/ControlServer_mac.mm
        Runtime/NodeManager_mac.mm
    )
endif()

add_library(owear_core STATIC ${OW_KERNEL_SOURCES})

if(OW_PLATFORM STREQUAL "linux")
    target_compile_definitions(owear_core PUBLIC OW_BUILTINS_GTK=1)
endif()

target_include_directories(owear_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_compile_definitions(owear_core PUBLIC OW_VERSION_STRING="0.1.0")

target_link_libraries(owear_core PUBLIC Threads::Threads ZLIB::ZLIB)

if(OW_PLATFORM STREQUAL "linux")
    target_include_directories(owear_core PRIVATE
        ${GTK3_INCLUDE_DIRS} ${WEBKIT_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIRS}
        ${GIO_UNIX_INCLUDE_DIRS})
    target_link_libraries(owear_core PUBLIC
        ${GTK3_LIBRARIES} ${WEBKIT_LIBRARIES} ${OPENSSL_LIBRARIES} ${GIO_UNIX_LIBRARIES})
    target_link_directories(owear_core PUBLIC
        ${GTK3_LIBRARY_DIRS} ${WEBKIT_LIBRARY_DIRS})
    target_compile_options(owear_core PRIVATE -Wall -Wextra)
endif()

# ── macOS: OpenSSL (Http) + GLib (ControlServer) + Cocoa/WebKit (UI) ────────
if(OW_PLATFORM STREQUAL "mac")
    target_include_directories(owear_core PRIVATE
        ${OPENSSL_INCLUDE_DIRS} ${GLIB2_INCLUDE_DIRS})
    target_link_libraries(owear_core PUBLIC
        ${OPENSSL_LIBRARIES} ${GLIB2_LIBRARIES}
        ${COCOA_FRAMEWORK} ${WEBKIT_FRAMEWORK})
endif()

# ── Binario del runtime ──────────────────────────────────────────────────────
add_executable(owear main_kernel.cpp)
target_link_libraries(owear PRIVATE owear_core)
if(MSVC)
    # Los .owm ven __declspec(dllimport) (OW_MODULE_BUILD en include/ow/Shm.h):
    # el exe del kernel debe exportar los símbolos (en Linux lo cubre -rdynamic).
    target_link_options(owear PRIVATE
        "/EXPORT:ow_shm_put" "/EXPORT:ow_shm_data" "/EXPORT:ow_shm_shutdown")
elseif(NOT WIN32)
    target_link_options(owear PRIVATE -rdynamic) # módulos .owm resuelven ow_shm_* del host
endif()
if(OW_PLATFORM STREQUAL "linux")
    target_compile_options(owear PRIVATE -Wall -Wextra)
endif()
