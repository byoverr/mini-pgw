# server library (for tests)
add_library(server_lib STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/server.cpp
)

target_include_directories(server_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${cpp_httplib_SOURCE_DIR}
)

target_link_libraries(server_lib PUBLIC
    common
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

if(ENABLE_COVERAGE AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
  target_compile_options(server_lib PRIVATE -g -O0 --coverage)
  target_link_options(server_lib PRIVATE --coverage)
endif()

# server executable
add_executable(pgw_server
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

target_include_directories(pgw_server PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${cpp_httplib_SOURCE_DIR}
)

target_link_libraries(pgw_server PRIVATE
    server_lib
)

if(ENABLE_COVERAGE AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
  target_compile_options(pgw_server PRIVATE -g -O0 --coverage)
  target_link_options(pgw_server PRIVATE --coverage)
endif()
