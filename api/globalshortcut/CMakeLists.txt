if(OW_PLATFORM STREQUAL "linux")
    find_package(X11 QUIET)
    if(NOT X11_FOUND)
        message(WARNING "globalshortcut: X11 no encontrada — API omitida (es OPCIONAL)")
        return()
    endif()
    set(GS_LIBS ${X11_LIBRARIES})
endif()
ow_add_module(globalshortcut
    SOURCES src/gs_${OW_PLATFORM}.cpp
    LIBS ${GS_LIBS} ${OW_GUI_LIBS}
    INCLUDES ${OW_GUI_INCS})
