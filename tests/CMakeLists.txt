# imsi_bcd
add_executable(imsi_bcd_test
    ${CMAKE_CURRENT_SOURCE_DIR}/imsi_bcd_test.cpp
)

target_include_directories(imsi_bcd_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(imsi_bcd_test PRIVATE
    common
    GTest::gtest_main
)

if(ENABLE_COVERAGE AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
  target_compile_options(imsi_bcd_test PRIVATE -g -O0 --coverage)
  target_link_options(imsi_bcd_test PRIVATE --coverage)
endif()

add_test(NAME IMSI_BCD_TEST COMMAND $<TARGET_FILE:imsi_bcd_test>)



# server tests
add_executable(server_test
    ${CMAKE_CURRENT_SOURCE_DIR}/server_test.cpp
)

target_include_directories(server_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/server
    ${cpp_httplib_SOURCE_DIR}
)

target_link_libraries(server_test PRIVATE
    server_lib
    common
    GTest::gtest_main
)

if(ENABLE_COVERAGE AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
  target_compile_options(server_test PRIVATE -g -O0 --coverage)
  target_link_options(server_test PRIVATE --coverage)
endif()

add_test(NAME SERVER_TEST COMMAND server_test)
