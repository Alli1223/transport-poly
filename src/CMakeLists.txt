# --- tp_core: platform independent utilities -------------------------------
file(GLOB_RECURSE TP_CORE_SOURCES CONFIGURE_DEPENDS core/*.cpp core/*.hpp)
add_library(tp_core STATIC ${TP_CORE_SOURCES})
target_include_directories(tp_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tp_core PUBLIC tp_options)

# --- tp_sim: the deterministic game simulation -------------------------------
file(GLOB_RECURSE TP_SIM_SOURCES CONFIGURE_DEPENDS sim/*.cpp sim/*.hpp)
add_library(tp_sim STATIC ${TP_SIM_SOURCES})
target_link_libraries(tp_sim PUBLIC tp_core)

# --- tp_net: sessions, server, networking -------------------------------------
file(GLOB_RECURSE TP_NET_SOURCES CONFIGURE_DEPENDS net/*.cpp net/*.hpp)
add_library(tp_net STATIC ${TP_NET_SOURCES})
target_link_libraries(tp_net PUBLIC tp_sim)
if(WIN32)
    target_link_libraries(tp_net PUBLIC ws2_32)
endif()

if(TP_BUILD_CLIENT)
    # --- tp_render: OpenGL renderer ----------------------------------------
    file(GLOB_RECURSE TP_RENDER_SOURCES CONFIGURE_DEPENDS render/*.cpp render/*.hpp render/*.inl)
    add_library(tp_render STATIC ${TP_RENDER_SOURCES})
    target_link_libraries(tp_render PUBLIC tp_sim tp_glm)

    # --- transport-poly: the game client -----------------------------------
    file(GLOB_RECURSE TP_CLIENT_SOURCES CONFIGURE_DEPENDS client/*.cpp client/*.hpp)
    add_executable(transport-poly ${TP_CLIENT_SOURCES})
    target_link_libraries(transport-poly PRIVATE tp_render tp_net tp_imgui glfw)
endif()

if(TP_BUILD_SERVER)
    # --- transport-poly-server: headless dedicated server ------------------
    file(GLOB_RECURSE TP_SERVER_SOURCES CONFIGURE_DEPENDS server/*.cpp server/*.hpp)
    add_executable(transport-poly-server ${TP_SERVER_SOURCES})
    target_link_libraries(transport-poly-server PRIVATE tp_net)
endif()
