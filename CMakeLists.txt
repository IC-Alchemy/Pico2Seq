cmake_minimum_required(VERSION 3.16)
project(Pico2Seq_Tests CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)

# Windows desktop port (branch x86): engine DLL for the WinUI front end.
# When enabled, this build tree uses the static CRT throughout (packaged
# MSIX apps cannot rely on the debug redistributables). The variable must be
# set before any targets are created, hence it precedes both subdirectories.
option(PICO2SEQ_BUILD_DESKTOP "Build the desktop engine DLL + golden tests" OFF)
if(PICO2SEQ_BUILD_DESKTOP)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_subdirectory(tests)

if(PICO2SEQ_BUILD_DESKTOP)
    add_subdirectory(desktop/native)
endif()
