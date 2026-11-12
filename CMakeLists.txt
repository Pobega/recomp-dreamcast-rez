cmake_minimum_required(VERSION 3.24)
project(rez_recomp VERSION 1.0.1 LANGUAGES C CXX)

# Rez (Dreamcast) as a native program. engine/ translates the game's SH-4 code to C++ at build time
# and provides the runtime it runs on; src/framework/ is the port layer (settings, HUD);
# src/ replaces Sega's libraries with native code; game/rez.toml describes the game.

set(DREAM_WERROR OFF CACHE BOOL "Warnings as errors in engine targets")

cmake_host_system_information(RESULT _dc_threads QUERY NUMBER_OF_LOGICAL_CORES)
if(_dc_threads LESS 1)
  set(_dc_threads 1)
elseif(_dc_threads GREATER 64)
  set(_dc_threads 64)
endif()
set(DREAM_GEN_SHARDS ${_dc_threads} CACHE STRING "Parts the generated game code is split into")

option(DREAM_STRICT_FP "Enforce strict cross-host floating-point rounding and disable FMA" ON)
set(DREAMCOMP_FP_OPTIONS
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:$<IF:$<BOOL:${DREAM_STRICT_FP}>,-ffp-contract=off,-ffp-contract=fast>>
    $<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:$<IF:$<BOOL:${DREAM_STRICT_FP}>,-ffp-contract=off,-ffp-contract=fast>>)
add_compile_options(${DREAMCOMP_FP_OPTIONS})
add_subdirectory(engine)
include(cmake/Port.cmake)

set(DREAMCOMP_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)

add_library(dreamcomp_core OBJECT src/framework/core.cpp src/framework/settings.cpp
                                  src/framework/hud.cpp src/framework/sharp2d.cpp
                                  src/framework/disc_folder.cpp)
target_include_directories(dreamcomp_core PUBLIC ${PROJECT_SOURCE_DIR}/include
                                                 ${PROJECT_SOURCE_DIR}/src/framework)
target_link_libraries(dreamcomp_core PUBLIC dream::runtime dream::render dream::game_config)
target_compile_features(dreamcomp_core PUBLIC cxx_std_20)
target_compile_options(dreamcomp_core PRIVATE ${DREAMCOMP_WARNINGS})
if(TARGET dream_render_vk)
  target_link_libraries(dreamcomp_core PUBLIC dream::render_vk)
  target_compile_definitions(dreamcomp_core PRIVATE DREAM_WITH_RENDERER)
endif()

add_subdirectory(game)
