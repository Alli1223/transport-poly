# --- tp_core: platform independent utilities -------------------------------
file(GLOB_RECURSE TP_CORE_SOURCES CONFIGURE_DEPENDS core/*.cpp core/*.hpp)
add_library(tp_core STATIC ${TP_CORE_SOURCES})
target_include_directories(tp_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tp_core PUBLIC tp_options)

if(TP_BUILD_CLIENT)
    # --- tp_render: OpenGL renderer ----------------------------------------
    file(GLOB_RECURSE TP_RENDER_SOURCES CONFIGURE_DEPENDS render/*.cpp render/*.hpp render/*.inl)
    add_library(tp_render STATIC ${TP_RENDER_SOURCES})
    target_link_libraries(tp_render PUBLIC tp_core tp_glm)

    # --- transport-poly: the game client -----------------------------------
    file(GLOB_RECURSE TP_CLIENT_SOURCES CONFIGURE_DEPENDS client/*.cpp client/*.hpp)
    add_executable(transport-poly ${TP_CLIENT_SOURCES})
    target_link_libraries(transport-poly PRIVATE tp_render tp_imgui glfw)
endif()

if(TP_BUILD_SERVER)
    # --- transport-poly-server: headless dedicated server ------------------
    file(GLOB_RECURSE TP_SERVER_SOURCES CONFIGURE_DEPENDS server/*.cpp server/*.hpp)
    add_executable(transport-poly-server ${TP_SERVER_SOURCES})
    target_link_libraries(transport-poly-server PRIVATE tp_core)
endif()
