#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "core/log.hpp"
#include "core/version.hpp"
#include "render/gl.hpp"

#include <cstdlib>

namespace {

void glfwErrorCallback(int code, const char* description) {
    tp::log::error("GLFW error {}: {}", code, description);
}

} // namespace

int main() {
    tp::log::info("{} {} starting", tp::kGameName, tp::kVersionString);

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) return EXIT_FAILURE;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    GLFWwindow* window = glfwCreateWindow(1600, 900, "transport-poly", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return EXIT_FAILURE;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!tp::gl::load(reinterpret_cast<tp::gl::LoadProc>(glfwGetProcAddress))) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }
    tp::log::info("OpenGL {} on {}", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
                  reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::Begin("transport-poly");
        ImGui::Text("Version %s", tp::kVersionString.data());
        ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);
        ImGui::End();
        ImGui::Render();

        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.55f, 0.75f, 0.95f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
