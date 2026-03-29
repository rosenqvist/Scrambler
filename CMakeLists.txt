cmake_minimum_required(VERSION 3.25)

project(Scrambler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SCRAMBLER_ENABLE_TESTS "Build unit tests" ON)
option(SCRAMBLER_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(SCRAMBLER_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(SCRAMBLER_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

add_library(scrambler_warnings INTERFACE)
target_compile_options(scrambler_warnings INTERFACE
    /W4
    -Wconversion
    -Wshadow
    -Wold-style-cast
    -Wnon-virtual-dtor
    -Woverloaded-virtual
    -Wnull-dereference
    -Wimplicit-fallthrough
)

add_library(scrambler_sanitizers INTERFACE)

if (SCRAMBLER_ENABLE_ASAN)
    target_compile_options(scrambler_sanitizers INTERFACE /fsanitize=address -fno-omit-frame-pointer)
    target_link_options(scrambler_sanitizers INTERFACE /fsanitize=address)
endif()

if (SCRAMBLER_ENABLE_UBSAN)
    target_compile_options(scrambler_sanitizers INTERFACE -fsanitize=undefined)
    target_link_options(scrambler_sanitizers INTERFACE -fsanitize=undefined)
endif()

if (SCRAMBLER_ENABLE_TSAN)
    target_compile_options(scrambler_sanitizers INTERFACE -fsanitize=thread)
    target_link_options(scrambler_sanitizers INTERFACE -fsanitize=thread)
endif()

add_library(WinDivert SHARED IMPORTED)
set_target_properties(WinDivert PROPERTIES
    IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/third_party/windivert/x64/WinDivert.dll"
    IMPORTED_IMPLIB "${CMAKE_SOURCE_DIR}/third_party/windivert/x64/WinDivert.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_party/windivert/include"
)

add_executable(scrambler
    src/main.cpp
)

target_include_directories(scrambler PRIVATE ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(scrambler PRIVATE
    WinDivert
    scrambler_warnings
    scrambler_sanitizers
)

add_custom_command(TARGET scrambler POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/third_party/windivert/x64/WinDivert.dll"
        "$<TARGET_FILE_DIR:scrambler>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/third_party/windivert/x64/WinDivert64.sys"
        "$<TARGET_FILE_DIR:scrambler>"
)

if (SCRAMBLER_ENABLE_TESTS)
    enable_testing()
    find_package(GTest CONFIG REQUIRED)
endif()

install(TARGETS scrambler RUNTIME DESTINATION .)
install(FILES
    "${CMAKE_SOURCE_DIR}/third_party/windivert/x64/WinDivert.dll"
    "${CMAKE_SOURCE_DIR}/third_party/windivert/x64/WinDivert64.sys"
    DESTINATION .
)
