cmake_minimum_required(VERSION 3.14)
project(MapleNight)

set(CMAKE_CXX_STANDARD 17)

# Load .env into process env so target_compile_definitions / output dir can pick it up.
# MN_ENV_FILE env var overrides the default .env path (used by tools/build-push.ps1
# to swap in .env.push without disturbing the local .env profile).
if (DEFINED ENV{MN_ENV_FILE})
    set(MN_ENV_FILE_PATH "$ENV{MN_ENV_FILE}")
else ()
    set(MN_ENV_FILE_PATH "${CMAKE_SOURCE_DIR}/.env")
endif ()
if (EXISTS ${MN_ENV_FILE_PATH})
    message(STATUS "Loading env from: ${MN_ENV_FILE_PATH}")
    file(STRINGS ${MN_ENV_FILE_PATH} ENV_FILE)
    foreach (VAR ${ENV_FILE})
        if (VAR MATCHES "^[ \t]*#" OR VAR MATCHES "^[ \t]*$")
            continue()
        endif ()
        string(REGEX MATCH "^[^=]*" KEY ${VAR})
        string(REGEX REPLACE "^[^=]*=" "" VALUE ${VAR})
        set(ENV{${KEY}} ${VALUE})
    endforeach ()
endif ()

if (DEFINED ENV{CUSTOM_BINARY_DIR})
    set(KAEN_OUTPUT_DIR "$ENV{CUSTOM_BINARY_DIR}")
else ()
    set(KAEN_OUTPUT_DIR "D:/MapleNightLocal")
endif ()
message(STATUS "Output dir: ${KAEN_OUTPUT_DIR}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${KAEN_OUTPUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${KAEN_OUTPUT_DIR}")
foreach(CFG DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG} "${KAEN_OUTPUT_DIR}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CFG} "${KAEN_OUTPUT_DIR}")
endforeach()

# Build external dependencies
add_subdirectory(external)

# Build injector and laucher
add_subdirectory(src)