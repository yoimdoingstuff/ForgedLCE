// ForgedLCE mod display strings: shared by the loader core and the game hooks.
#ifndef FLCE_STRINGS_H
#define FLCE_STRINGS_H

namespace ForgedLCE {

// String ids at or above this belong to ForgedLCE (names of mod blocks and
// items). The game's own string ids are far below it.
const int STRING_ID_BASE = 0x40000000;

// The text for a ForgedLCE string id, or NULL if `id` isn't one.
const wchar_t *GetModString(int id);

} // namespace ForgedLCE

#endif
