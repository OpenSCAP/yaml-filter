cmake_minimum_required(VERSION 2.8)
project(yaml-path C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

include(FindPkgConfig)
pkg_check_modules(YAML yaml-0.1)
find_package(codecov)

include_directories(${YAML_INCLUDE_DIRS} src)

add_library(yaml-path src/yaml-path.c)
target_link_libraries(yaml-path ${YAML_LIBRARIES})
add_coverage(yaml-path)

add_executable(yamlp src/yamlp.c)
target_link_libraries(yamlp yaml-path)
add_coverage(yamlp)

install(TARGETS yamlp RUNTIME DESTINATION bin)

if(${CMAKE_C_COMPILER_ID} STREQUAL "GNU" OR ${CMAKE_C_COMPILER_ID} STREQUAL "Clang")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pipe -std=c99 -W -Wall -Wnonnull -Wshadow -Wformat -Wundef -Wno-unused-parameter -Wmissing-prototypes -Wno-unknown-pragmas -D_GNU_SOURCE -D_POSIX_C_SOURCE=200112L")
endif()
if(${CMAKE_SYSTEM_NAME} EQUAL "Solaris")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D__EXTENSIONS__")
endif()
if(WIN32)
	# Expose new WinAPI function appearing on Windows 7 (e.g. inet_pton)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_WIN32_WINNT=0x0600")
endif()
if(APPLE)
	# Full Single Unix Standard v3 (SUSv3) conformance (the Unix API)
	add_definitions(-D_DARWIN_C_SOURCE)
endif()

enable_testing()
add_subdirectory("tests")

coverage_evaluate()

