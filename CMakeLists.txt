cmake_minimum_required(VERSION 3.21)

project(
	Rapport
	VERSION 0.2.2
	LANGUAGES CXX
)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "")

# One build for OG 1.10.163, NG 1.10.984 and AE 1.11.x through Runtime Database
# (CommonLibF4RD, owner 2026-09-26): addresses resolved at run time from f4rd-runtime.bin,
# the class layouts Rapport reads guarded at run time. OFF builds the classic OG-only
# plugin on alandtse's CommonLibF4, which refuses any other runtime.
option(RAPPORT_RUNTIME_DATABASE "Build on CommonLibF4RD for OG, NG and AE" ON)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
if(NOT RAPPORT_RUNTIME_DATABASE)
	set(ENABLE_FALLOUT_F4 ON  CACHE BOOL "" FORCE)
	set(ENABLE_FALLOUT_NG OFF CACHE BOOL "" FORCE)
	set(ENABLE_FALLOUT_VR OFF CACHE BOOL "" FORCE)
endif()

# __FILE__ without this machine's folders (dev-vs-release check, 2026-09-26): the
# compiler writes every source's full path into the dll, which named the Windows
# user in a stranger's download. Trimmed here, before CommonLibF4, so its paths go
# too, and in the build rather than the packaging, so the dev dll and the release
# dll stay one build. Only string literals change; Rapport's log names no files.
if(MSVC)
	file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/" RAPPORT_TRIM_PREFIX)
	add_compile_options("/d1trimfile:${RAPPORT_TRIM_PREFIX}")
endif()

if(RAPPORT_RUNTIME_DATABASE)
	add_subdirectory("extern/CommonLibF4RD/CommonLibF4" "CommonLibF4")
else()
	add_subdirectory("extern/CommonLibF4/CommonLibF4" "CommonLibF4")
endif()

set(SOURCES
	"src/PCH.h"
	"src/Barks.h"
	"src/Barks.cpp"
	"src/McmSettings.cpp"
	"src/Narrator.cpp"
	"src/Names.h"
	"src/Names.cpp"
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
	"src/Orientation.h"
	"src/Orientation.cpp"
	"src/DialogueVoice.h"
	"src/DialogueVoice.cpp"
	"src/DebugHub.h"
	"src/DebugHub.cpp"
	"src/NamedLock.h"
	"src/Orders.h"
	"src/Aftermath.h"
	"src/Aftermath.cpp"
	"src/Morphs.cpp"
	"src/Expressions.h"
	"src/Expressions.cpp"
	"src/ForeignScenes.h"
	"src/ForeignScenes.cpp"
	"src/Placement.cpp"
	"src/DebugTriggers.h"
	"src/DebugTriggers.cpp"
	"src/FaceAuthority.h"
	"src/FaceAuthority.cpp"
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
		"$<$<BOOL:${RAPPORT_RUNTIME_DATABASE}>:RP_RUNTIME_DATABASE=1>"
)

target_include_directories("${PROJECT_NAME}" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries("${PROJECT_NAME}" PRIVATE CommonLibF4::CommonLibF4)

target_precompile_headers("${PROJECT_NAME}" PRIVATE "src/PCH.h")

if(MSVC)
	target_compile_options("${PROJECT_NAME}" PRIVATE "/permissive-" "/Zc:preprocessor" "/utf-8")
endif()
