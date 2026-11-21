cmake_minimum_required(VERSION 3.21)

set(ANTIGRAVITY_PLUGIN_VERSION "1.0.0" CACHE STRING "Antigravity plugin version")

project(AntigravityPlugin VERSION "${ANTIGRAVITY_PLUGIN_VERSION}" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Shared plugin library
add_library(AntigravityPlugin SHARED
    src/AntigravityPlugin.cpp
    src/QuotaSync.cpp
    src/AntigravityPlugin.def
)

target_include_directories(AntigravityPlugin PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_definitions(AntigravityPlugin PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if(MSVC)
    target_compile_options(AntigravityPlugin PRIVATE /utf-8 /O2)
endif()

target_link_libraries(AntigravityPlugin PRIVATE
    crypt32
    advapi32
    gdi32
    gdiplus
    user32
    winhttp
    ws2_32
    bcrypt
    shell32
)

set_target_properties(AntigravityPlugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "AntigravityPlugin"
)

# Test / Verification executable
add_executable(verify_plugin
    tests/verify_plugin.cpp
)

target_include_directories(verify_plugin PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_definitions(verify_plugin PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if(MSVC)
    target_compile_options(verify_plugin PRIVATE /utf-8)
endif()

target_link_libraries(verify_plugin PRIVATE
    gdi32
    user32
)

# Smart Mode state transitions test executable
add_executable(test_smart_transitions
    tests/test_smart_transitions.cpp
    src/AntigravityPlugin.cpp
    src/QuotaSync.cpp
)

target_include_directories(test_smart_transitions PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_definitions(test_smart_transitions PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if(MSVC)
    target_compile_options(test_smart_transitions PRIVATE /utf-8)
endif()

target_link_libraries(test_smart_transitions PRIVATE
    crypt32
    advapi32
    gdi32
    gdiplus
    user32
    winhttp
    ws2_32
    bcrypt
    shell32
)

enable_testing()
add_test(NAME verify_plugin COMMAND verify_plugin $<TARGET_FILE:AntigravityPlugin>)
add_test(NAME test_smart_transitions COMMAND test_smart_transitions)
