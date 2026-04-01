cmake_minimum_required(VERSION 3.25)

project(Scrambler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

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
    src/core/FlowTracker.cpp
    src/core/PacketInterceptor.cpp
    src/core/DelayQueue.cpp
    src/ui/MainWindow.cpp
    src/ui/HotkeyManager.cpp
    src/platform/ProcessEnumerator.cpp
    include/ui/MainWindow.h
    include/ui/HotkeyManager.h
    include/ui/HotkeyEdit.h
)

# Hide console window in release builds
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    set_target_properties(scrambler PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# Embed icon directly into binary
qt_add_resources(scrambler "app_icons"
    PREFIX "/icons"
    BASE "resources"
    FILES
        resources/scrambler_icon_256.png
)

# Embed application manifest via resource file
if (WIN32)
    target_sources(scrambler PRIVATE
        ${CMAKE_SOURCE_DIR}/resources/scrambler.rc
    )

    target_link_options(scrambler PRIVATE
        "/MANIFEST:NO"
    )
endif()

target_include_directories(scrambler PRIVATE ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(scrambler PRIVATE
    Qt6::Widgets
    WinDivert
    winmm
    dwmapi
    ws2_32
    iphlpapi
    scrambler_warnings
    scrambler_sanitizers
)

# Pick correct Qt platform plugin for the build type
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(QT_PLATFORM_DIR "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/debug/Qt6/plugins/platforms")
    set(QT_PLATFORM_DLL "qwindowsd.dll")
else()
    set(QT_PLATFORM_DIR "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/Qt6/plugins/platforms")
    set(QT_PLATFORM_DLL "qwindows.dll")
endif()

# Copy runtime dependencies next to the executable
add_custom_command(TARGET scrambler POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/third_party/windivert/x64/WinDivert.dll"
        "$<TARGET_FILE_DIR:scrambler>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/third_party/windivert/x64/WinDivert64.sys"
        "$<TARGET_FILE_DIR:scrambler>"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:scrambler>/platforms"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${QT_PLATFORM_DIR}/${QT_PLATFORM_DLL}"
        "$<TARGET_FILE_DIR:scrambler>/platforms/"
)

# Deploy Qt runtime DLLs and plugins for distribution
if (WIN32)
    qt_generate_deploy_app_script(
        TARGET scrambler
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT ${deploy_script})
endif()

if (SCRAMBLER_ENABLE_TESTS)
    enable_testing()
    find_package(GTest CONFIG REQUIRED)

    add_executable(scrambler_tests
        tests/FiveTupleTest.cpp
        tests/TargetSetTest.cpp
        tests/EffectConfigTest.cpp
        tests/ProcessEnumeratorTest.cpp
        src/platform/ProcessEnumerator.cpp
    )

    target_include_directories(scrambler_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )

    target_link_libraries(scrambler_tests PRIVATE
        GTest::gtest_main
        WinDivert
        ws2_32
        iphlpapi
        scrambler_warnings
        scrambler_sanitizers
    )

    # Copy WinDivert DLL next to test executable so it can load at runtime
    add_custom_command(TARGET scrambler_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/third_party/windivert/x64/WinDivert.dll"
            "$<TARGET_FILE_DIR:scrambler_tests>"
    )

    include(GoogleTest)
    gtest_discover_tests(scrambler_tests)
endif()

install(TARGETS scrambler RUNTIME DESTINATION .)
install(FILES
    "${CMAKE_SOURCE_DIR}/third_party/windivert/x64/WinDivert.dll"
    "${CMAKE_SOURCE_DIR}/third_party/windivert/x64/WinDivert64.sys"
    DESTINATION .
)
