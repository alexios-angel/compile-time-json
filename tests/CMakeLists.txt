# the suite is static_asserts: compiling a test IS running it
add_custom_target(ctjson-test ALL)

file(GLOB test-sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# tests with a main() run; the rest are static_assert suites where
# compiling is the test
set(runtime-tests dumps load)

foreach(source ${test-sources})
  get_filename_component(test "${source}" NAME_WE)
  if(test IN_LIST runtime-tests)
    add_executable(ctjson-test-${test} "${source}")
    target_link_libraries(ctjson-test-${test} PRIVATE ctjson::ctjson)
    if(CTJSON_PCH AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.16")
      target_precompile_headers(ctjson-test-${test} PRIVATE
        "${PROJECT_SOURCE_DIR}/include/ctjson.hpp")
    endif()
    add_dependencies(ctjson-test ctjson-test-${test})
    add_test(NAME run-${test} COMMAND ctjson-test-${test})
  else()
    add_library(ctjson-test-${test} OBJECT EXCLUDE_FROM_ALL "${source}")
    target_link_libraries(ctjson-test-${test} PRIVATE ctjson::ctjson)
    if(CTJSON_PCH AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.16")
      target_precompile_headers(ctjson-test-${test} PRIVATE
        "${PROJECT_SOURCE_DIR}/include/ctjson.hpp")
    endif()
    add_dependencies(ctjson-test ctjson-test-${test})
    add_test(NAME compile-${test}
      COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --target ctjson-test-${test})
  endif()
endforeach()
