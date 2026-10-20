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
    target_link_libraries(transport-poly PRIVATE tp_render tp_net tp_imgui glfw tp_audio)
    # Ship the UI font next to the executable.
    add_custom_command(TARGET transport-poly POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:transport-poly>/fonts
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TP_IMGUI_FONT_DIR}/Roboto-Medium.ttf
                $<TARGET_FILE_DIR:transport-poly>/fonts/Roboto-Medium.ttf)
endif()

if(TP_BUILD_SERVER)
    # --- transport-poly-server: headless dedicated server ------------------
    file(GLOB_RECURSE TP_SERVER_SOURCES CONFIGURE_DEPENDS server/*.cpp server/*.hpp)
    add_executable(transport-poly-server ${TP_SERVER_SOURCES})
    target_link_libraries(transport-poly-server PRIVATE tp_net)
endif()

# --- Visual Studio: solution folders, startup project and debugger directory ----
set_target_properties(tp_core tp_sim tp_net PROPERTIES FOLDER "Libraries")
if(TP_BUILD_CLIENT)
    set_target_properties(tp_render PROPERTIES FOLDER "Libraries")
    set_target_properties(transport-poly PROPERTIES FOLDER "Game"
        VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:transport-poly>")
    set_property(DIRECTORY ${PROJECT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT transport-poly)
endif()
if(TP_BUILD_SERVER)
    set_target_properties(transport-poly-server PROPERTIES FOLDER "Game"
        VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:transport-poly-server>")
endif()

# --- Installation (used by CPack) ----------------------------------------------
# Everything goes into one directory: the client finds its font next to the executable.
if(WIN32)
    set(TP_INSTALL_BINDIR .)
else()
    set(TP_INSTALL_BINDIR bin)
endif()
if(TP_BUILD_CLIENT)
    install(TARGETS transport-poly RUNTIME DESTINATION ${TP_INSTALL_BINDIR})
    install(FILES ${TP_IMGUI_FONT_DIR}/Roboto-Medium.ttf DESTINATION ${TP_INSTALL_BINDIR}/fonts)
    install(FILES ${miniaudio_SOURCE_DIR}/LICENSE DESTINATION licenses RENAME miniaudio.txt)
endif()
if(TP_BUILD_SERVER)
    install(TARGETS transport-poly-server RUNTIME DESTINATION ${TP_INSTALL_BINDIR})
endif()
install(FILES ${PROJECT_SOURCE_DIR}/README.md ${PROJECT_SOURCE_DIR}/CHANGELOG.md DESTINATION .)
if(WIN32)
    # Ship the MSVC runtime so the zip runs without the redistributable installed.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${TP_INSTALL_BINDIR})
    include(InstallRequiredSystemLibraries)
endif()
