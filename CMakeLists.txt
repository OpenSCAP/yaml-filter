cmake_minimum_required(VERSION 2.8)

include(FindPkgConfig)
pkg_check_modules(YAML yaml-0.1)

include_directories(${YAML_INCLUDE_DIRS})

add_library(yaml-path yaml-path.c)
target_link_libraries(yaml-path ${YAML_LIBRARIES})

add_executable(ya yaml.c)
target_link_libraries(ya yaml-path)

add_executable(yamlp yamlp.c)
target_link_libraries(yamlp yaml-path)

install(TARGETS ya RUNTIME DESTINATION bin)
install(TARGETS yamlp RUNTIME DESTINATION bin)

add_executable(test-path-segments test-path-segments.c yaml-path.c)

add_executable(test-paths test-paths.c)
target_link_libraries(test-paths yaml-path)

enable_testing()
add_test(NAME "test-path-segments" COMMAND ${CMAKE_BINARY_DIR}/test-path-segments)
add_test(NAME "test-paths" COMMAND ${CMAKE_BINARY_DIR}/test-paths)
add_test(NAME "test-yamlp" COMMAND ${CMAKE_SOURCE_DIR}/test-yamlp.sh)
