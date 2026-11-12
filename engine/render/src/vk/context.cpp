#include "dream/render/vk/context.h"

#include <cstring>
#include <GLES3/gl3.h>
#include <SDL.h>

namespace dream::render::vk {

namespace {

constexpr GLenum kGlLowerLeftExt = 0x8CA1;
constexpr GLenum kGlZeroToOneExt = 0x935F;
using PFNGLCLIPCONTROLEXTPROC = void (*)(GLenum, GLenum);

}  // namespace

bool Context::create(const std::vector<const char*>&) {
    const GLubyte* renderer = glGetString(GL_RENDERER);
    caps_.device_name = renderer ? reinterpret_cast<const char*>(renderer) : "OpenGL ES 3.0";
    GLint max_tex = 2048;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex);
    caps_.max_texture_size = static_cast<std::uint32_t>(max_tex);

    GLint num_extensions = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &num_extensions);
    for (GLint i = 0; i < num_extensions; ++i) {
        const GLubyte* ext = glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i));
        if (ext && std::strcmp(reinterpret_cast<const char*>(ext), "GL_EXT_clip_control") == 0) {
            auto clip_control = reinterpret_cast<PFNGLCLIPCONTROLEXTPROC>(SDL_GL_GetProcAddress("glClipControlEXT"));
            if (clip_control) {
                clip_control(kGlLowerLeftExt, kGlZeroToOneExt);
                caps_.zero_to_one_depth = true;
            }
            break;
        }
    }

    return true;
}

void Context::destroy() {
}

}  // namespace dream::render::vk
