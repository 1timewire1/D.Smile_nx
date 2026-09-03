cmake_minimum_required(VERSION 3.22.1)
project(dsmile C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-O3 -fno-exceptions -fno-rtti -Wall)
# Android's ucontext headers define REG_R0..REG_R7 which collide with register enums.
add_compile_options(-UREG_R0 -UREG_R1 -UREG_R2 -UREG_R3 -UREG_R4 -UREG_R5 -UREG_R6 -UREG_R7)

find_package(oboe REQUIRED CONFIG)

add_library(dsmile SHARED
    core/unsp.cpp
    core/spg200.cpp
    core/ppu.cpp
    core/spu.cpp
    core/vsmile.cpp
    jni_bridge.cpp
)

target_include_directories(dsmile PRIVATE .)
# The AGP/NDK toolchain handoff drops the STL from the link line in this
# configuration (ANDROID_STL never reaches CMAKE_ANDROID_STL_TYPE), so link
# libc++_shared explicitly. AGP still packages it via the ANDROID_STL argument.
target_link_libraries(dsmile android log oboe::oboe c++_shared)
