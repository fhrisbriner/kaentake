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
#define CONFIG_NEXON_SEARCH     "63.251.217."

// ---- Monster Book ----
// Each module compiles to nothing when its flag is FALSE, and the flags are independent, with one
// documented exception: monsterBookSearch.cpp READS the 16-per-page Dropping paging that
// monsterBookDrops.cpp installs. It guards for that rather than assuming it, so turning DROPS off
// degrades the search drop-icon click instead of breaking it.
#define USE_MONSTER_BOOK_OPEN    TRUE  // icons, counters, art/HP/MP, all four tabs
#define USE_MONSTER_BOOK_FOUNDIN TRUE  // Found In row click -> world map, text restyling
#define USE_MONSTER_BOOK_DROPS   TRUE  // drop chance % label on the Dropping icons
#define USE_MONSTER_BOOK_SEARCH  TRUE  // mob-name + item-name search fields

extern int g_ScreenPosX;
extern int g_ScreenPosY;



extern char* g_sServerHost;
extern long g_nServerPort;