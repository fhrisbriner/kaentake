// HAND MAINTAINED. Add ids here, in any order, wherever you like.
//
// Maps that are INDOORS and so get no weather at all: no sky, no rain or snow, no
// clouds, no moon, no day/night tint, no lamps, and no sway on anything the map
// contains. A room with a roof has no weather and no wind, and the two go together.
//
// WHY THIS EXISTS ALONGSIDE weather_nosky_maps.inc, which does most of the same job:
// that list is the accumulated result of auditing the sky table against the whole map
// set, it is long, it is cited from its own header, and churning it to try one id is
// how a curated list stops being trustworthy. This one is the scratch list -- add,
// test, keep or delete. Both are merged into one sorted index at first use, so an id
// in either has exactly the same effect and neither file's ORDER matters any more.
//
// The one thing this list does that weather_nosky_maps.inc does not is silence SWAY.
// A map named only there keeps its bending ropes and plants, which is deliberate for
// the tree dungeons and the shop fronts already on it; a map named HERE goes quiet.
//
// Format is one `id,` per line. The trailing comment is for whoever reads it next --
// an id with no name attached is unreviewable, so write down which map it is.
//
// The -1 is a sentinel and must stay: an empty initialiser list is not a legal array in
// C++, and this file is legitimately allowed to be empty. Negative ids are dropped when
// the index is built, so it costs nothing and no real field id can collide with it.

    -1,   // sentinel -- leave in place, add real ids below


    // e.g.
    //  103000000,   // Kerning City : Subway Ticketing Booth
