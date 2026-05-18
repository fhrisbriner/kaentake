cmake_minimum_required(VERSION 3.14)
project(MapleNight)

set(CMAKE_CXX_STANDARD 17)

# Load .env into process env so target_compile_definitions / output dir can pick it up
if (EXISTS ${CMAKE_SOURCE_DIR}/.env)
    file(STRINGS ${CMAKE_SOURCE_DIR}/.env ENV_FILE)
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
    set(KAEN_OUTPUT_DIR "D:/SkirchData2")
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