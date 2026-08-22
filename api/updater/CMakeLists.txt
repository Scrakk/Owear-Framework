find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)
ow_add_module(updater
    SOURCES
        src/updater.cpp
        src/updater_platform_${OW_PLATFORM}.${OW_SRC_EXT}
        ../../src/Runtime/Http.cpp
        ../../src/Runtime/Sha256.cpp
    LIBS OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB Threads::Threads
    INCLUDES ${CMAKE_SOURCE_DIR}/src)
