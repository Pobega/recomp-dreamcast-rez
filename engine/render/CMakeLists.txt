# dream::render: display-list decoding, texture decoding, and the unified
# OpenGL ES 3.0 + SDL2 renderer backend.
add_library(dream_render STATIC
  src/display_list.cpp
  src/texture.cpp
  src/texture_pack.cpp
  src/png.cpp
  src/framebuffer.cpp
  src/background.cpp
  src/fog.cpp
  src/overlay.cpp
  src/interpolate.cpp
  src/input.cpp
  src/input_menu.cpp
)
target_include_directories(dream_render PUBLIC include)
target_link_libraries(dream_render PUBLIC dream::runtime)
dream_target_defaults(dream_render)
add_library(dream::render ALIAS dream_render)

if(NOT DREAM_RENDERER)
  return()
endif()

find_path(DREAM_SDL2_INCLUDE_DIR SDL.h PATH_SUFFIXES SDL2)
find_library(DREAM_SDL2_LIBRARY NAMES SDL2)
if(NOT DREAM_SDL2_INCLUDE_DIR OR NOT DREAM_SDL2_LIBRARY)
  message(STATUS "dream::render: no SDL2; the renderer is skipped")
  return()
endif()

find_path(DREAM_GLES3_INCLUDE_DIR GLES3/gl3.h)
find_library(DREAM_GLES3_LIBRARY NAMES GLESv2)
if(NOT DREAM_GLES3_INCLUDE_DIR OR NOT DREAM_GLES3_LIBRARY)
  message(STATUS "dream::render: no GLESv2; the renderer is skipped")
  return()
endif()

add_library(dream_render_vk STATIC
  src/vk/context.cpp
  src/vk/window_sdl2.cpp
  src/vk/resources.cpp
  src/vk/renderer.cpp
  src/vk/texture_cache.cpp
  src/vk/offscreen.cpp
  src/vk/present.cpp
)
target_include_directories(dream_render_vk PUBLIC include ${DREAM_GLES3_INCLUDE_DIR}
                           PRIVATE ${DREAM_SDL2_INCLUDE_DIR})
target_link_libraries(dream_render_vk PUBLIC dream::render
                      ${DREAM_SDL2_LIBRARY} ${DREAM_GLES3_LIBRARY})
dream_target_defaults(dream_render_vk)
add_library(dream::render_vk ALIAS dream_render_vk)

message(STATUS "dream::render: OpenGL ES 3.0 + SDL2 backend enabled")
