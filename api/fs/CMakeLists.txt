# ── API fs ────────────────────────────────────────────────────────────────────
# Operaciones de filesystem completas: básicas, metadatos, handles fd-style
# y watcher multiplataforma (inotify / ReadDirectoryChangesW / kqueue).

if(OW_PLATFORM STREQUAL "linux")
    find_package(Threads REQUIRED)
    set(OW_FS_LIBS Threads::Threads)
endif()

ow_add_module(fs
    SOURCES
        src/basic.cpp
        src/meta.cpp
        src/handles.cpp
        src/watch/watch.cpp
        src/watch/watch_${OW_PLATFORM}.cpp
    LIBS ${OW_FS_LIBS})
