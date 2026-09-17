cmake_minimum_required(VERSION 3.20)
project(PhoneCursorServer CXX)

if(NOT WIN32)
    message(FATAL_ERROR "desktop_server only builds for Windows (uses Winsock and the Win32 input APIs).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(PhoneCursorServer
    src/main.cpp
    src/UdpServer.cpp
    src/InputSimulator.cpp
    src/PacketHandler.cpp
    src/MotionProcessor.cpp
)

target_include_directories(PhoneCursorServer PRIVATE src)
target_link_libraries(PhoneCursorServer PRIVATE ws2_32 user32)
