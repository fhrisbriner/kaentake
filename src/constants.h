#pragma once

#define CONSTANTS_WINDOW_NAME "Maple Night"
#define CONSTANTS_DLL_NAME    "MapleNight.dll"
#define CONSTANTS_CONFIG_NAME "config.ini"

#define CONSTANTS_CENTER_STATUSBAR TRUE

#define LOCALHOST "15.204.66.127"

#if defined(MN_LOCAL_BUILD) && MN_LOCAL_BUILD
  #define CONSTANTS_DEFAULT_HOST LOCALHOST
#else
  #define CONSTANTS_DEFAULT_HOST "15.204.66.127"
#endif
#define CONSTANTS_USE_COMMAND_LINE FALSE
#define CONSTANTS_USE_CONFIG_FILE  FALSE

extern char* g_sServerHost;
extern long g_nServerPort;