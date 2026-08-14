# ARCL frontend for the sibling TOWNSEMU core targets.
# This file is included by TOWNSEMU/src/CMakeLists.txt; it is not a standalone
# CMake project because it deliberately links the core targets defined there.

add_library(arcl_vm_runner src/arcl_vm_runner.cpp src/arcl_vm_runner.h)
target_include_directories(arcl_vm_runner PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(arcl_vm_runner PUBLIC towns)

add_library(arcl_capture_host src/arcl_capture_host.cpp src/arcl_capture_host.h)
target_include_directories(arcl_capture_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(arcl_capture_host PUBLIC outside_world yssimplesound)

add_library(arcl_window src/arcl_window.cpp src/arcl_window.h)
target_include_directories(arcl_window PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(arcl_window PUBLIC arcl_capture_host fssimplewindow fssimplewindow_connection)

add_library(arcl_mcp_server src/arcl_mcp_server.cpp src/arcl_mcp_server.h)
target_include_directories(arcl_mcp_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(arcl_windows_fmtowns src/arcl_main.cpp)
target_link_libraries(arcl_windows_fmtowns PRIVATE arcl_vm_runner arcl_window arcl_mcp_server townsargv townsrender yspng)
set_target_properties(arcl_windows_fmtowns PROPERTIES
	OUTPUT_NAME arcl_windows_fmtowns
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_test(NAME ARCL_COMMAND_LINE COMMAND arcl_windows_fmtowns --arcl-help)

add_executable(arcl_mcp_server_test src/arcl_mcp_server_test.cpp)
target_link_libraries(arcl_mcp_server_test PRIVATE arcl_mcp_server)
add_test(NAME ARCL_MCP_SERVER COMMAND arcl_mcp_server_test)

add_executable(arcl_vm_runner_test src/arcl_vm_runner_test.cpp)
target_link_libraries(arcl_vm_runner_test PRIVATE arcl_vm_runner)
add_test(NAME ARCL_VM_RUNNER_BOUNDARY COMMAND arcl_vm_runner_test)

add_executable(arcl_capture_host_test src/arcl_capture_host_test.cpp)
target_link_libraries(arcl_capture_host_test PRIVATE arcl_capture_host)
add_test(NAME ARCL_CAPTURE_HOST COMMAND arcl_capture_host_test)
