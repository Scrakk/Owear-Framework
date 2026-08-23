if(OW_PLATFORM STREQUAL "mac")
    set(OW_POWER_LIBS "-framework Cocoa" "-framework IOKit")
endif()
ow_add_module(power    SOURCES src/power_${OW_PLATFORM}.${OW_SRC_EXT}
    LIBS ${OW_GUI_LIBS} ${OW_POWER_LIBS}
    INCLUDES ${OW_GUI_INCS})
