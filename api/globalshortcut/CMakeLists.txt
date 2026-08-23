if(OW_PLATFORM STREQUAL "linux")
    find_package(X11 QUIET)
    if(NOT X11_FOUND)
        message(WARNING "globalshortcut: X11 no encontrada — API omitida (es OPCIONAL)")
        return()
    endif()
    set(GS_LIBS ${X11_LIBRARIES})
endif()
if(OW_PLATFORM STREQUAL "mac")
    set(GS_LIBS "-framework Carbon")
endif()
ow_add_module(globalshortcut
    SOURCES src/gs_${OW_PLATFORM}.${OW_SRC_EXT}
    LIBS ${GS_LIBS} ${OW_GUI_LIBS}
    INCLUDES ${OW_GUI_INCS})
