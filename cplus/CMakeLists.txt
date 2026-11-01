cmake_minimum_required(VERSION 3.15)
project(WeatherWidget LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# WebView2 SDK paths
set(WEBVIEW2_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sdk")
set(WEBVIEW2_INCLUDE_DIR "${WEBVIEW2_SDK_DIR}/include")

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(WEBVIEW2_LIB_DIR "${WEBVIEW2_SDK_DIR}/lib/x64")
    set(ARCH_SUFFIX "x64")
else()
    set(WEBVIEW2_LIB_DIR "${WEBVIEW2_SDK_DIR}/lib/x86")
    set(ARCH_SUFFIX "x86")
endif()

add_executable(WeatherWidget WIN32 WeatherWidget.cpp)

target_include_directories(WeatherWidget PRIVATE ${WEBVIEW2_INCLUDE_DIR})
target_link_directories(WeatherWidget PRIVATE ${WEBVIEW2_LIB_DIR})

target_link_libraries(WeatherWidget
    user32
    gdi32
    ws2_32
    winhttp
    crypt32
    dwmapi
    shlwapi
    winmm
    ole32
    oleaut32
)

# The WebView2 loader is loaded at runtime (LoadLibrary), so no import library
# is needed - this keeps the build free of MSVC-only static libraries.

target_compile_definitions(WeatherWidget PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    target_compile_options(WeatherWidget PRIVATE /utf-8 /W3)
    target_link_options(WeatherWidget PRIVATE /SUBSYSTEM:WINDOWS)
else()
    # -fms-extensions lets GCC parse the WebView2 SDK headers (MIDL_INTERFACE / __declspec)
    target_compile_options(WeatherWidget PRIVATE -fms-extensions -finput-charset=UTF-8 -fexec-charset=UTF-8 -Wall)
    target_link_options(WeatherWidget PRIVATE -mwindows)
endif()

# Shared frontend UI located at the project root (WeatherWidget/weather-widget.html)
set(SHARED_HTML "${CMAKE_CURRENT_SOURCE_DIR}/../weather-widget.html")

# Copy WebView2Loader.dll and weather-widget.html to output directory
add_custom_command(TARGET WeatherWidget POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${WEBVIEW2_LIB_DIR}/WebView2Loader.dll"
        "$<TARGET_FILE_DIR:WeatherWidget>/WebView2Loader.dll"
)

add_custom_command(TARGET WeatherWidget POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SHARED_HTML}"
        "$<TARGET_FILE_DIR:WeatherWidget>/weather-widget.html"
)
