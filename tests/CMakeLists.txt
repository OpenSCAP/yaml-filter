function(_add_test TEST_NAME TEST_COMMAND)
	add_test(NAME ${TEST_NAME} COMMAND ${TEST_COMMAND})
	set_tests_properties(${TEST_NAME} PROPERTIES
		SKIP_RETURN_CODE 255
		ENVIRONMENT "CURRENT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR};SOURCE_DIR=${CMAKE_SOURCE_DIR};BINARY_DIR=${CMAKE_BINARY_DIR}"
	)
endfunction()

function(add_test_executable EXECUTABLE_NAME SOURCE_FILE)
	set(TEST_EXECUTABLE_LIBRARIES yaml-path)
	add_executable(${EXECUTABLE_NAME} ${SOURCE_FILE} ${ARGN})
	target_link_libraries(${EXECUTABLE_NAME} ${TEST_EXECUTABLE_LIBRARIES})
	target_include_directories(${EXECUTABLE_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
	add_coverage(${EXECUTABLE_NAME})
	string(REPLACE "${CMAKE_SOURCE_DIR}/tests/" "" TEST_NAME "${CMAKE_CURRENT_SOURCE_DIR}/${EXECUTABLE_NAME}")
	_add_test(${TEST_NAME} "${CMAKE_BINARY_DIR}/tests/${EXECUTABLE_NAME}")
endfunction()

function(add_test_script TEST_SCRIPT)
	string(REPLACE "${CMAKE_SOURCE_DIR}/tests/" "" TEST_NAME "${CMAKE_CURRENT_SOURCE_DIR}/${TEST_SCRIPT}")
	_add_test(${TEST_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/${TEST_SCRIPT}")
endfunction()

add_test_executable(test-path-segments test-path-segments.c)
add_test_executable(test-paths test-paths.c)
add_test_script(test-yamlp.sh)

list(APPEND LCOV_REMOVE_PATTERNS "'${CMAKE_SOURCE_DIR}/tests/*'")
