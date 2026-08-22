# ── API process ──────────────────────────────────────────────────────────────
# spawn de procesos con pipes de stdout/stderr/stdin + PTY Manager real.
#   Linux/Mac: forkpty(3)          Windows: ConPTY (CreatePseudoConsole)

if(OW_PLATFORM STREQUAL "linux")
    find_package(Threads REQUIRED)
    set(OW_PROC_LIBS Threads::Threads util)
elseif(OW_PLATFORM STREQUAL "mac")
    find_package(Threads REQUIRED)
    set(OW_PROC_LIBS Threads::Threads "-framework Foundation")
endif()

set(OW_PROC_SOURCES
    src/module.cpp
    src/spawn_${OW_PLATFORM}.${OW_SRC_EXT}
    src/pty/pty_${OW_PLATFORM}.${OW_SRC_EXT})
if(OW_PLATFORM STREQUAL "linux")
    list(APPEND OW_PROC_SOURCES src/spawn_linux.cpp) # helpers compartidos pipe-reader
endif()

ow_add_module(process
    SOURCES ${OW_PROC_SOURCES}
    LIBS ${OW_PROC_LIBS})
