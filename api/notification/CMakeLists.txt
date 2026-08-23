if(OW_PLATFORM STREQUAL "win")
    set(OW_NOTIF_LIBS Shell32 User32)
endif()
ow_add_module(notification    SOURCES src/notification_${OW_PLATFORM}.${OW_SRC_EXT}
    LIBS ${OW_GUI_LIBS} ${OW_NOTIF_LIBS}
    INCLUDES ${OW_GUI_INCS})
