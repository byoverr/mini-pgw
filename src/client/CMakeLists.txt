add_executable(pgw_client
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

target_include_directories(pgw_client PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(pgw_client PRIVATE
    common
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

if(ENABLE_COVERAGE AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
  target_compile_options(pgw_client PRIVATE -g -O0 --coverage)
  target_link_options(pgw_client PRIVATE --coverage)
endif()
