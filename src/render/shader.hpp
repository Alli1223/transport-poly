#pragma once

#include "render/gl_util.hpp"

#include <glm/glm.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace tp::gfx {

// Binding point of the per-frame uniform block shared by all programs.
inline constexpr GLuint kFrameUniformBinding = 0;

class Shader {
public:
    // Compiles and links a program. On failure logs the info log and returns nullopt.
    static std::optional<Shader> create(std::string_view name, std::string_view vertexSource,
                                        std::string_view fragmentSource);

    void use() const { glUseProgram(program_.get()); }
    GLuint id() const { return program_.get(); }
    GLint location(const char* name) const;

    void set(const char* name, int v) const { glUniform1i(location(name), v); }
    void set(const char* name, float v) const { glUniform1f(location(name), v); }
    void set(const char* name, const glm::vec2& v) const { glUniform2f(location(name), v.x, v.y); }
    void set(const char* name, const glm::vec3& v) const { glUniform3f(location(name), v.x, v.y, v.z); }
    void set(const char* name, const glm::vec4& v) const { glUniform4f(location(name), v.x, v.y, v.z, v.w); }
    void set(const char* name, const glm::mat4& m) const { glUniformMatrix4fv(location(name), 1, GL_FALSE, &m[0][0]); }

private:
    Program program_;
    std::string name_;
    mutable std::unordered_map<std::string, GLint> locations_;
};

} // namespace tp::gfx
