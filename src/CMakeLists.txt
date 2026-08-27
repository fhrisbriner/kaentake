# Build injector
add_library(injector SHARED
        injector.cpp
        system.cpp
        bypass.cpp
        inlink.cpp
        resman.cpp
        avatar.cpp
        itemeff.cpp
        cashweapon.cpp
        resolution.cpp
        mobhptag.cpp
        tooltip.cpp
        itemicon.cpp
        tempstat.cpp
        stringpool.cpp
        debugflags.cpp
        memstat.cpp
        deathcount.cpp
        mistexplosion.cpp
        getmobinfobyid.cpp
        monsterBook.cpp
        monsterBookFoundIn.cpp
        monsterBookDrops.cpp
        monsterBookSearch.cpp
        debug.cpp
        hook.cpp
        skills.cpp
        skillupgrade.cpp
        skillupgrade.h
        packet.cpp
        mapfadeobject.cpp
        mapfadeobject.h
        ztl/zmap.h
        wvs/CWvsContext.h
        wvs/CUserLocal.h
        wvs/mob.h
        storagebag.cpp
        weapontint.cpp
        weapontint.h
        coloringprism.cpp

        # Weather and world clock. weather.cpp owns the six shared CMapLoadable hooks and the
        # CreateLayer capture chain every other module here files its layers through, so the
        # first five are one unit: none of them links without the others. Our own day/night
        # (server-side background swap) stays the night look -- weather.cpp's tint and
        # weatherfx.cpp's injected sky/moon are held off at their kWeatherNightVisuals switch.
        weather.cpp
        weatherfx.cpp
        weathercanvas.cpp
        weathersound.cpp
        weathersplash.cpp
        weatherwind.cpp
        weatheraccum.cpp
        weatherpuddle.cpp
        weathermove.cpp
        weathersway.cpp
        # Compiled but dark: weather.cpp calls into lamps.cpp unguarded, so the file ships and
        # the layer is switched off inside it (kLampsEnabled).
        lamps.cpp
        weather.h
        weatherfx.h
        weathercanvas.h
        lamps.h
        patch_common.h
)

target_precompile_headers(injector PRIVATE pch.h)

target_link_libraries(injector PRIVATE
        Detours
        WzLib
)

# Pin the DLL to its preferred base (0x10000000) instead of letting ASLR relocate it.
# 22 CodeCave/PatchJmp sites patch `E9 rel32` jumps from the client image (~0x00400000) into this
# DLL, and rel32 is a signed 32-bit displacement (+-2GB). Without LARGE_ADDRESS_AWARE that always
# holds, because nothing can live above 0x80000000. If MapleStory.exe is ever made LAA, a DLL
# relocated into the upper 2GB would make every one of those jumps wrap to a wild address --
# intermittently, per-machine, depending on the base ASLR picked. Pinning removes the variable.
# /MAP: a crash reported by Windows Error Reporting gives only a module-relative fault
# offset, and without a symbol table that number cannot be turned into a function name.
target_link_options(injector PRIVATE "/DYNAMICBASE:NO" "/MAP")

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

# MSVC runtime is set globally (static) in the root CMakeLists via
# CMAKE_MSVC_RUNTIME_LIBRARY + CMP0091, so all targets -- injector, launcher,
# updater, and the static deps (Detours/WzLib) -- link the CRT statically and
# match. No per-target override needed.

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