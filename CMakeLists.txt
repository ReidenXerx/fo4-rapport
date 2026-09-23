cmake_minimum_required(VERSION 3.21)

project(
	Rapport
	VERSION 0.2.0
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
	"src/Barks.h"
	"src/Barks.cpp"
	"src/McmSettings.cpp"
	"src/Narrator.cpp"
	"src/Watchers.h"
	"src/Watchers.cpp"
	"src/Voices.h"
	"src/Voices.cpp"
	"src/Config.h"
	"src/Config.cpp"
	"src/Mailbox.h"
	"src/Mailbox.cpp"
	"src/ActorScan.h"
	"src/ActorScan.cpp"
	"src/Pairing.h"
	"src/Pairing.cpp"
	"src/DebugHub.h"
	"src/DebugHub.cpp"
	"src/NamedLock.h"
	"src/Orders.h"
	"src/Aftermath.h"
	"src/Aftermath.cpp"
	"src/Morphs.cpp"
	"src/Expressions.h"
	"src/Expressions.cpp"
	"src/Ledger.h"
	"src/Ledger.cpp"
	"src/AAFHealth.h"
	"src/AAFHealth.cpp"
	"src/Scenarios.h"
	"src/Scenarios.cpp"
	"src/TreeIndex.h"
	"src/TreeIndex.cpp"
	"src/Takeover.h"
	"src/Takeover.cpp"
	"src/PapyrusLink.h"
	"src/PapyrusLink.cpp"
	"src/Scheduler.h"
	"src/Candidates.cpp"
	"src/Scheduler.cpp"
	"src/main.cpp"
)

add_library("${PROJECT_NAME}" SHARED ${SOURCES})

target_compile_features("${PROJECT_NAME}" PRIVATE cxx_std_23)

target_compile_definitions(
	"${PROJECT_NAME}"
	PRIVATE
		"RP_PROJECT_NAME=\"${PROJECT_NAME}\""
		"RP_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}"
		"RP_VERSION_MINOR=${PROJECT_VERSION_MINOR}"
		"RP_VERSION_PATCH=${PROJECT_VERSION_PATCH}"
		"RP_VERSION_STRING=\"${PROJECT_VERSION}\""
)

target_include_directories("${PROJECT_NAME}" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries("${PROJECT_NAME}" PRIVATE CommonLibF4::CommonLibF4)

target_precompile_headers("${PROJECT_NAME}" PRIVATE "src/PCH.h")

if(MSVC)
	target_compile_options("${PROJECT_NAME}" PRIVATE "/permissive-" "/Zc:preprocessor" "/utf-8")
endif()
