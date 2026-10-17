#pragma once
// RAII wrappers for OpenGL object names.

#include "render/gl.hpp"

#include <utility>

namespace tp::gfx {

template <class Deleter>
class GlHandle {
public:
    GlHandle() = default;
    explicit GlHandle(GLuint id) : id_(id) {}
    ~GlHandle() { reset(); }
    GlHandle(GlHandle&& o) noexcept : id_(std::exchange(o.id_, 0)) {}
    GlHandle& operator=(GlHandle&& o) noexcept {
        if (this != &o) {
            reset();
            id_ = std::exchange(o.id_, 0);
        }
        return *this;
    }
    GlHandle(const GlHandle&) = delete;
    GlHandle& operator=(const GlHandle&) = delete;

    void reset() {
        if (id_) {
            Deleter{}(id_);
            id_ = 0;
        }
    }
    GLuint get() const { return id_; }
    explicit operator bool() const { return id_ != 0; }

private:
    GLuint id_ = 0;
};

struct BufferDeleter {
    void operator()(GLuint id) const { glDeleteBuffers(1, &id); }
};
struct VertexArrayDeleter {
    void operator()(GLuint id) const { glDeleteVertexArrays(1, &id); }
};
struct TextureDeleter {
    void operator()(GLuint id) const { glDeleteTextures(1, &id); }
};
struct FramebufferDeleter {
    void operator()(GLuint id) const { glDeleteFramebuffers(1, &id); }
};
struct RenderbufferDeleter {
    void operator()(GLuint id) const { glDeleteRenderbuffers(1, &id); }
};
struct ProgramDeleter {
    void operator()(GLuint id) const { glDeleteProgram(id); }
};

using Buffer = GlHandle<BufferDeleter>;
using VertexArray = GlHandle<VertexArrayDeleter>;
using Texture = GlHandle<TextureDeleter>;
using Framebuffer = GlHandle<FramebufferDeleter>;
using Renderbuffer = GlHandle<RenderbufferDeleter>;
using Program = GlHandle<ProgramDeleter>;

inline Buffer makeBuffer() {
    GLuint id = 0;
    glGenBuffers(1, &id);
    return Buffer(id);
}
inline VertexArray makeVertexArray() {
    GLuint id = 0;
    glGenVertexArrays(1, &id);
    return VertexArray(id);
}
inline Texture makeTexture() {
    GLuint id = 0;
    glGenTextures(1, &id);
    return Texture(id);
}
inline Framebuffer makeFramebuffer() {
    GLuint id = 0;
    glGenFramebuffers(1, &id);
    return Framebuffer(id);
}
inline Renderbuffer makeRenderbuffer() {
    GLuint id = 0;
    glGenRenderbuffers(1, &id);
    return Renderbuffer(id);
}

} // namespace tp::gfx
