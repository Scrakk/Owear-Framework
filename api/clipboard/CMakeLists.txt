if(OW_PLATFORM STREQUAL "win")
    set(OW_CLIP_LIBS Gdiplus Ole32 User32)
endif()
ow_add_module(clipboard
    SOURCES src/clipboard_${OW_PLATFORM}.${OW_SRC_EXT}
    LIBS ${OW_GUI_LIBS} ${OW_CLIP_LIBS}
    INCLUDES ${OW_GUI_INCS})
