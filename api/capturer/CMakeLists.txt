if(OW_PLATFORM STREQUAL "linux")
    find_package(X11 QUIET)
    if(NOT X11_FOUND)
        message(WARNING "capturer: X11 no encontrada — API omitida")
        return()
    endif()
endif()
if(OW_PLATFORM STREQUAL "win")
    set(OW_CAP_LIBS Gdiplus Ole32 User32)
endif()
ow_add_module(capturer
    SOURCES src/capturer_${OW_PLATFORM}.${OW_SRC_EXT}
    LIBS ${OW_GUI_LIBS} ${OW_CAP_LIBS}
    INCLUDES ${OW_GUI_INCS})
