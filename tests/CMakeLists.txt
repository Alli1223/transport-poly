file(GLOB_RECURSE TP_TEST_SOURCES CONFIGURE_DEPENDS *.cpp *.hpp)
add_executable(tp_tests ${TP_TEST_SOURCES})
target_link_libraries(tp_tests PRIVATE tp_net)
add_test(NAME tp_tests COMMAND tp_tests)
