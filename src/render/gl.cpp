#include "render/gl.hpp"

#include "core/log.hpp"

#define TP_GL_FN(ret, name, params) PFN_##name tp_##name = nullptr;
#define TP_GL_FN_OPTIONAL TP_GL_FN
#include "render/gl_functions.inl"
#undef TP_GL_FN
#undef TP_GL_FN_OPTIONAL

namespace tp::gl {

bool load(LoadProc getProcAddress) {
    bool ok = true;
#define TP_GL_FN(ret, name, params)                                                                          \
    tp_##name = reinterpret_cast<PFN_##name>(getProcAddress(#name));                                         \
    if (!tp_##name) {                                                                                        \
        log::error("OpenGL: missing required entry point {}", #name);                                       \
        ok = false;                                                                                          \
    }
#define TP_GL_FN_OPTIONAL(ret, name, params) tp_##name = reinterpret_cast<PFN_##name>(getProcAddress(#name));
#include "render/gl_functions.inl"
#undef TP_GL_FN
#undef TP_GL_FN_OPTIONAL
    return ok;
}

bool hasDebugOutput() { return tp_glDebugMessageCallback != nullptr; }

} // namespace tp::gl
