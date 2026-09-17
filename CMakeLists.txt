cmake_minimum_required(VERSION 3.21)

project(
	AutonomyFramework
	VERSION 0.1.0
	LANGUAGES CXX
)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "")

# We target Fallout 4 OG 1.10.163 only. Next-gen and VR are deliberately off:
# every RelocationID then resolves to the one runtime we test against.
set(ENABLE_FALLOUT_F4 ON  CACHE BOOL "" FORCE)
set(ENABLE_FALLOUT_NG OFF CACHE BOOL "" FORCE)
set(ENABLE_FALLOUT_VR OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS       OFF CACHE BOOL "" FORCE)

add_subdirectory("extern/CommonLibF4/CommonLibF4" "CommonLibF4")

set(SOURCES
	"src/PCH.h"
	"src/Config.h"
	"src/Config.cpp"
	"src/ActorScan.h"
	"src/ActorScan.cpp"
	"src/Scheduler.h"
	"src/Scheduler.cpp"
	"src/main.cpp"
)

add_library("${PROJECT_NAME}" SHARED ${SOURCES})

target_compile_features("${PROJECT_NAME}" PRIVATE cxx_std_23)

target_compile_definitions(
	"${PROJECT_NAME}"
	PRIVATE
		"AF_PROJECT_NAME=\"${PROJECT_NAME}\""
		"AF_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}"
		"AF_VERSION_MINOR=${PROJECT_VERSION_MINOR}"
		"AF_VERSION_PATCH=${PROJECT_VERSION_PATCH}"
		"AF_VERSION_STRING=\"${PROJECT_VERSION}\""
)

target_include_directories("${PROJECT_NAME}" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries("${PROJECT_NAME}" PRIVATE CommonLibF4::CommonLibF4)

target_precompile_headers("${PROJECT_NAME}" PRIVATE "src/PCH.h")

if(MSVC)
	target_compile_options("${PROJECT_NAME}" PRIVATE "/permissive-" "/Zc:preprocessor" "/utf-8")
endif()
