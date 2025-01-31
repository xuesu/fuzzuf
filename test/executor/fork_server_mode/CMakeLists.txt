add_executable( test-executor-run-fork run.cpp )
target_link_libraries(
  test-executor-run-fork
  test-common
  fuzzuf
  ${FUZZUF_LIBRARIES}
  Boost::unit_test_framework
)
target_include_directories(
  test-executor-run-fork
  PRIVATE
  ${FUZZUF_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}/test/common
)
set_target_properties(
  test-executor-run-fork
  PROPERTIES COMPILE_FLAGS "${ADDITIONAL_COMPILE_FLAGS_STR}"
)
set_target_properties(
  test-executor-run-fork
  PROPERTIES LINK_FLAGS "${ADDITIONAL_COMPILE_FLAGS_STR}"
)
add_test( NAME "native_linux_executor.native_linux_context.fork.run" 
        COMMAND test-executor-run-fork )
if( ENABLE_CLANG_TIDY )
  set_target_properties(
    test-executor-run-non-fork
    PROPERTIES
    CXX_CLANG_TIDY "${CLANG_TIDY};${CLANG_TIDY_CONFIG_FOR_TEST}"
  )
endif()
add_test( NAME "native_linux_executor.native_linux_context.non-fork.run" 
          COMMAND test-executor-run-non-fork )

