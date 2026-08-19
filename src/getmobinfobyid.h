#pragma once
#include <string>

// String/Mob.img name resolver. Returns "Unknown" when the id has no entry, which is what the
// Monster Book search treats as "fall through to my own table" -- so the sentinel matters and
// must not be changed to an empty string.
std::string GetMobNameById(int nMobId);
