cmake_minimum_required(VERSION 3.16)
project(PhotoCircleAnimator LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Photo Circle Animator currently requires Windows.")
endif()

add_executable(PhotoCircleAnimator WIN32 main.cpp)
target_compile_features(PhotoCircleAnimator PRIVATE cxx_std_17)
target_link_libraries(PhotoCircleAnimator PRIVATE gdiplus comdlg32 gdi32 user32)
