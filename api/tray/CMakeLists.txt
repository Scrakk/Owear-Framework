# ── tray ── libayatana-appindicator (opcional) ──────────────────────────────
# pkg_check_modules se confunde con "-0.1" en el nombre → usamos pkg-config directo.
if(OW_PLATFORM STREQUAL "linux")
    execute_process(COMMAND pkg-config --exists ayatana-appindicator3-0.1
                    RESULT_VARIABLE _have_ayatana ERROR_QUIET OUTPUT_QUIET)
    if(NOT _have_ayatana EQUAL 0)
        execute_process(COMMAND pkg-config --exists appindicator3-0.1
                        RESULT_VARIABLE _have_appind ERROR_QUIET OUTPUT_QUIET)
    endif()

    if(_have_ayatana EQUAL 0 OR _have_appind EQUAL 0)
        set(TRAY_PKG_NAME "ayatana-appindicator3-0.1")
        if(_have_appind EQUAL 0 AND NOT _have_ayatana EQUAL 0)
            set(TRAY_PKG_NAME "appindicator3-0.1")
        else()
            add_definitions(-DHAVE_AYATANA=1)
        endif()
        execute_process(COMMAND pkg-config --cflags ${TRAY_PKG_NAME}
                        OUTPUT_VARIABLE TRAY_CFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
        execute_process(COMMAND pkg-config --libs ${TRAY_PKG_NAME}
                        OUTPUT_VARIABLE TRAY_LIBS_STR OUTPUT_STRIP_TRAILING_WHITESPACE)
        separate_arguments(TRAY_CFLAGS)
        separate_arguments(TRAY_LIBS_STR)
        message(STATUS "tray: usando ${TRAY_PKG_NAME}")
    else()
        message(WARNING "tray: libayatana-appindicator no encontrada — API omitida "
                        "(sudo apt install libayatana-appindicator3-dev)")
        return()
    endif()
endif()

if(OW_PLATFORM STREQUAL "win")
    set(TRAY_LIBS_STR "Shell32;User32;Gdi32")
endif()

ow_add_module(tray
    SOURCES src/tray_${OW_PLATFORM}.${OW_SRC_EXT}
    LIBS ${TRAY_LIBS_STR})
target_compile_options(owear-api-tray PRIVATE ${TRAY_CFLAGS})
