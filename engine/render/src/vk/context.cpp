#include "dream/render/vk/context.h"

#include <GLES3/gl3.h>

namespace dream::render::vk {

bool Context::create(const std::vector<const char*>&) {
    const GLubyte* renderer = glGetString(GL_RENDERER);
    caps_.device_name = renderer ? reinterpret_cast<const char*>(renderer) : "OpenGL ES 3.0";
    GLint max_tex = 2048;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex);
    caps_.max_texture_size = static_cast<std::uint32_t>(max_tex);
    return true;
}

void Context::destroy() {
}

}  // namespace dream::render::vk
