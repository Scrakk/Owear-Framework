# Tests unitarios (assert-based, cero deps). Un ejecutable por suite.

set(OW_TEST_LIBS owear_core)

add_executable(owear_test_minjson minjson_test.cpp)
target_include_directories(owear_test_minjson PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
add_test(NAME minjson COMMAND owear_test_minjson)

add_executable(owear_test_codec codec_test.cpp)
target_include_directories(owear_test_codec PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(owear_test_codec PRIVATE ${OW_TEST_LIBS})
add_test(NAME codec COMMAND owear_test_codec)

add_executable(owear_test_sha256 sha256_test.cpp)
target_include_directories(owear_test_sha256 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(owear_test_sha256 PRIVATE ${OW_TEST_LIBS})
add_test(NAME sha256 COMMAND owear_test_sha256)

# F3.1 — registro de memoria compartida
add_executable(owear_test_shm shm_test.cpp)
target_include_directories(owear_test_shm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(owear_test_shm PRIVATE ${OW_TEST_LIBS})
add_test(NAME shm COMMAND owear_test_shm)
