#include "render/shader.hpp"

#include "core/log.hpp"

#include <vector>

namespace tp::gfx {

namespace {

GLuint compileStage(GLenum stage, std::string_view source, std::string_view name) {
    const GLuint shader = glCreateShader(stage);
    const GLchar* src = source.data();
    const GLint len = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &src, &len);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint logLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> logText(static_cast<size_t>(logLen > 1 ? logLen : 1));
        glGetShaderInfoLog(shader, logLen, nullptr, logText.data());
        log::error("Shader '{}' ({}) failed to compile:\n{}", name, stage == GL_VERTEX_SHADER ? "vertex" : "fragment",
                   logText.data());
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

std::optional<Shader> Shader::create(std::string_view name, std::string_view vertexSource,
                                     std::string_view fragmentSource) {
    const GLuint vs = compileStage(GL_VERTEX_SHADER, vertexSource, name);
    if (!vs) return std::nullopt;
    const GLuint fs = compileStage(GL_FRAGMENT_SHADER, fragmentSource, name);
    if (!fs) {
        glDeleteShader(vs);
        return std::nullopt;
    }
    Program program(glCreateProgram());
    glAttachShader(program.get(), vs);
    glAttachShader(program.get(), fs);
    glLinkProgram(program.get());
    glDetachShader(program.get(), vs);
    glDetachShader(program.get(), fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(program.get(), GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint logLen = 0;
        glGetProgramiv(program.get(), GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> logText(static_cast<size_t>(logLen > 1 ? logLen : 1));
        glGetProgramInfoLog(program.get(), logLen, nullptr, logText.data());
        log::error("Shader '{}' failed to link:\n{}", name, logText.data());
        return std::nullopt;
    }

    const GLuint blockIndex = glGetUniformBlockIndex(program.get(), "FrameData");
    if (blockIndex != GL_INVALID_INDEX) glUniformBlockBinding(program.get(), blockIndex, kFrameUniformBinding);

    Shader shader;
    shader.program_ = std::move(program);
    shader.name_ = std::string(name);
    return shader;
}

GLint Shader::location(const char* name) const {
    const auto it = locations_.find(name);
    if (it != locations_.end()) return it->second;
    const GLint loc = glGetUniformLocation(program_.get(), name);
    locations_.emplace(name, loc);
    return loc;
}

} // namespace tp::gfx
