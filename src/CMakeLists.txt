# Build injector
add_library(injector SHARED
        injector.cpp
        system.cpp
        bypass.cpp
        inlink.cpp
        resman.cpp
        avatar.cpp
        itemeff.cpp
        resolution.cpp
        mobhptag.cpp
        tooltip.cpp
        itemicon.cpp
        tempstat.cpp
        stringpool.cpp
        debug.cpp
        hook.cpp
        skills.cpp
        packet.cpp
        mapfadeobject.cpp
        mapfadeobject.h
        ztl/zmap.h
        wvs/CWvsContext.h
        wvs/CUserLocal.h
        wvs/mob.h
)

target_precompile_headers(injector PRIVATE pch.h)

target_link_libraries(injector PRIVATE
        Detours
        WzLib
)

set_target_properties(injector PROPERTIES
        OUTPUT_NAME ${PROJECT_NAME}
)


# Build launcher
add_executable(launcher WIN32
        launcher.manifest
        launcher.rc
        launcher.cpp
        debug.cpp
)

target_link_options(launcher PRIVATE "/MANIFEST:NO")

target_link_libraries(launcher PRIVATE
        Detours
        gdiplus.lib
        urlmon.lib
)


# Build updater (CLI invoked by launcher; protocol documented at top of updater.cpp)
add_executable(updater
        updater.cpp
)

target_link_libraries(updater PRIVATE
        winhttp.lib
        bcrypt.lib
        shell32.lib
)

set_target_properties(updater PROPERTIES
        OUTPUT_NAME Updater
)

if (MSVC)
    set_property(TARGET injector PROPERTY
            MSVC_RUNTIME_LIBRARY "$<$<CONFIG:Debug>:MultiThreadedDebugDLL>$<$<NOT:$<CONFIG:Debug>>:MultiThreaded>")
    set_property(TARGET launcher PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET updater PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()

foreach (MN_LAUNCHER_ENV_KEY
        MN_UPDATE_ENABLED
        MN_UPDATE_LATEST_VERSION
        MN_UPDATE_BASE_URL
        MN_RUNTIME_VERSION
        MN_UPDATE_S3_ENDPOINT
        MN_UPDATE_S3_BUCKET
        MN_UPDATE_S3_REGION)
    if (DEFINED ENV{${MN_LAUNCHER_ENV_KEY}})
        string(REPLACE "\"" "\\\"" MN_LAUNCHER_ENV_VALUE "$ENV{${MN_LAUNCHER_ENV_KEY}}")
        target_compile_definitions(launcher PRIVATE "${MN_LAUNCHER_ENV_KEY}=\"${MN_LAUNCHER_ENV_VALUE}\"")
    endif ()
endforeach ()

# Local-build flag: when MN_UPDATE_ENABLED=0 the build is meant for the dev's own
# machine, so constants.h picks LOCALHOST instead of the public server IP.
if (DEFINED ENV{MN_UPDATE_ENABLED} AND "$ENV{MN_UPDATE_ENABLED}" STREQUAL "0")
    target_compile_definitions(injector PRIVATE MN_LOCAL_BUILD=1)
    target_compile_definitions(launcher PRIVATE MN_LOCAL_BUILD=1)
endif ()

set_target_properties(launcher PROPERTIES
        OUTPUT_NAME ${PROJECT_NAME}
)