find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)
ow_add_module(net
    SOURCES
        src/net.cpp
        ../../src/Runtime/Http.cpp
        ../../src/Runtime/Sha256.cpp
    LIBS OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB Threads::Threads
    INCLUDES ${CMAKE_SOURCE_DIR}/src)
