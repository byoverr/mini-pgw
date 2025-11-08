add_library(common STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/imsi_to_bcd.cpp
)

target_include_directories(common PUBLIC
    ${CMAKE_SOURCE_DIR}/src/common
)

