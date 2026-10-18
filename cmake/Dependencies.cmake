# Third-party dependencies, fetched at configure time and pinned to release tags.
include(FetchContent)

if(TP_BUILD_CLIENT)
    # --- GLFW: window, OpenGL context and input -------------------------------
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    if(UNIX AND NOT APPLE)
        option(TP_GLFW_WAYLAND "Build GLFW with native Wayland support (X11 is always built)" OFF)
        set(GLFW_BUILD_WAYLAND ${TP_GLFW_WAYLAND} CACHE BOOL "" FORCE)
        set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
    endif()
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
        GIT_SHALLOW TRUE
        SYSTEM)

    # --- GLM: maths for the renderer (never used by the deterministic simulation)
    FetchContent_Declare(glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.1
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR _header_only_no_cmake)

    # --- Dear ImGui: user interface --------------------------------------------
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.91.9b
        GIT_SHALLOW TRUE)

    # Optional local sound output; miniaudio uses native Windows / Linux backends.
    FetchContent_Declare(miniaudio
        GIT_REPOSITORY https://github.com/mackron/miniaudio.git
        GIT_TAG 0.11.23
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR _header_only_no_cmake)
    FetchContent_MakeAvailable(glfw glm imgui miniaudio)
    add_library(tp_audio STATIC ${PROJECT_SOURCE_DIR}/src/audio/audio.cpp)
    target_include_directories(tp_audio PUBLIC ${PROJECT_SOURCE_DIR}/src)
    target_include_directories(tp_audio SYSTEM PRIVATE ${miniaudio_SOURCE_DIR})
    target_link_libraries(tp_audio PRIVATE tp_options ${CMAKE_DL_LIBS})
    if(UNIX)
        find_package(Threads REQUIRED)
        target_link_libraries(tp_audio PRIVATE Threads::Threads m)
    endif()
    set_target_properties(tp_audio PROPERTIES FOLDER "Libraries")

    add_library(tp_glm INTERFACE)
    target_include_directories(tp_glm SYSTEM INTERFACE ${glm_SOURCE_DIR})
    target_compile_definitions(tp_glm INTERFACE GLM_FORCE_SILENT_WARNINGS GLM_ENABLE_EXPERIMENTAL)

    add_library(tp_imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
    target_include_directories(tp_imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_compile_definitions(tp_imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
    target_link_libraries(tp_imgui PUBLIC glfw ${CMAKE_DL_LIBS})
    set_target_properties(tp_imgui PROPERTIES FOLDER "third_party")
    set(TP_IMGUI_FONT_DIR ${imgui_SOURCE_DIR}/misc/fonts CACHE INTERNAL "")
endif()
