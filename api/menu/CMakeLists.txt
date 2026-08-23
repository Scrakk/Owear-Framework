if(OW_PLATFORM STREQUAL "mac")
    set(OW_MENU_LIBS "-framework Cocoa")
endif()
if(OW_PLATFORM STREQUAL "win")
    set(OW_MENU_LIBS User32)
endif()
ow_add_module(menu    SOURCES src/menu_${OW_PLATFORM}.${OW_SRC_EXT}
    LIBS ${OW_GUI_LIBS} ${OW_MENU_LIBS}
    INCLUDES ${OW_GUI_INCS})
