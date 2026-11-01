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
    uuid
    WebView2LoaderStatic.lib
)

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
    target_compile_options(WeatherWidget PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8 -Wall)
    target_link_options(WeatherWidget PRIVATE -mwindows)
endif()

# Copy WebView2Loader.dll and weather-widget.html to output directory
add_custom_command(TARGET WeatherWidget POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${WEBVIEW2_LIB_DIR}/WebView2Loader.dll"
        "$<TARGET_FILE_DIR:WeatherWidget>/WebView2Loader.dll"
)

add_custom_command(TARGET WeatherWidget POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/weather-widget.html"
        "$<TARGET_FILE_DIR:WeatherWidget>/weather-widget.html"
)
