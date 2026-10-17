# Shared compile options for all first-party targets.
add_library(tp_options INTERFACE)
target_compile_features(tp_options INTERFACE cxx_std_23)

if(MSVC)
    target_compile_options(tp_options INTERFACE
        /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /EHsc
        /wd4324  # structure padded due to alignment specifier
    )
    target_compile_definitions(tp_options INTERFACE
        NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
    if(TP_WARNINGS_AS_ERRORS)
        target_compile_options(tp_options INTERFACE /WX)
    endif()
else()
    target_compile_options(tp_options INTERFACE
        -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
    if(TP_WARNINGS_AS_ERRORS)
        target_compile_options(tp_options INTERFACE -Werror)
    endif()
endif()

find_package(Threads REQUIRED)
target_link_libraries(tp_options INTERFACE Threads::Threads)
