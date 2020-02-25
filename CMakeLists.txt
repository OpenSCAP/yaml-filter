cmake_minimum_required(VERSION 2.8)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

include(FindPkgConfig)
pkg_check_modules(YAML yaml-0.1)
find_package(codecov)

include_directories(${YAML_INCLUDE_DIRS})

add_library(yaml-path yaml-path.c)
target_link_libraries(yaml-path ${YAML_LIBRARIES})

add_executable(ya yaml.c)
target_link_libraries(ya yaml-path)
add_coverage(ya)

add_executable(yamlp yamlp.c)
target_link_libraries(yamlp yaml-path)
add_coverage(yamlp)

add_coverage(yaml-path)

install(TARGETS ya RUNTIME DESTINATION bin)
install(TARGETS yamlp RUNTIME DESTINATION bin)

add_executable(test-path-segments test-path-segments.c yaml-path.c)
add_coverage(test-path-segments)

add_executable(test-paths test-paths.c)
target_link_libraries(test-paths yaml-path)
add_coverage(test-paths)
list(APPEND LCOV_REMOVE_PATTERNS "'${CMAKE_SOURCE_DIR}/test-*'")

coverage_evaluate()

enable_testing()
add_test(NAME "test-path-segments" COMMAND ${CMAKE_BINARY_DIR}/test-path-segments)
add_test(NAME "test-paths" COMMAND ${CMAKE_BINARY_DIR}/test-paths)
add_test(NAME "test-yamlp" COMMAND ${CMAKE_SOURCE_DIR}/test-yamlp.sh)
