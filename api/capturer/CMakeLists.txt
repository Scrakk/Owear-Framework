if(OW_PLATFORM STREQUAL "linux")
    find_package(X11 QUIET)
    if(NOT X11_FOUND)
        message(WARNING "capturer: X11 no encontrada — API omitida")
        return()
    endif()
endif()
ow_add_module(capturer
    SOURCES src/capturer_${OW_PLATFORM}.${OW_SRC_EXT}
    LIBS ${OW_GUI_LIBS}
    INCLUDES ${OW_GUI_INCS})
