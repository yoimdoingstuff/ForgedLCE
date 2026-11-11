// ForgedLCE: PowerPC (Xbox 360) function-hook builder.
//
// Pure functions that compute instruction words; they never touch memory,
// so they're unit-tested on a PC. The Xbox 360 plugin writes the results.
//
// A hook overwrites the first 4 instructions of the target with an absolute
// jump to the replacement:
//     lis r11, hi(replacement) ; ori r11, r11, lo(replacement) ; mtctr r11 ; bctr
// and builds a trampoline that runs those 4 original instructions, then
// jumps to target + 16, so calling the trampoline runs the original function.
//
// Relative branches in the first 4 instructions can't be copied as they are
// (the trampoline lives far from the game's code), so:
//   b  X  ->  lis r11 / ori r11 / mtctr r11 / bctr    (absolute jump)
//   bl X  ->  lis r11 / ori r11 / mtctr r11 / bctrl   (absolute call; MSVC
//             prologues start with `mflr r12; bl __savegprlr_NN`)
// Relative conditional branches are refused. r11 and CTR are volatile in the
// PowerPC calling convention, so they're free to use at a function's entry.
#ifndef FLCE_HOOKPPC_H
#define FLCE_HOOKPPC_H

namespace ForgedLCE {
namespace PPC {

typedef unsigned int Word;

const Word NOP   = 0x60000000;
const Word BCTR  = 0x4E800420;
const Word BCTRL = 0x4E800421;

// The 4-instruction absolute jump (or call, with BCTRL) to `dest` via r11.
void EncodeJump(Word dest, Word out[4], bool link = false);

// Most words a trampoline needs: 4 instructions that may each expand to 4,
// plus the 4-word jump back.
const int MAX_TRAMPOLINE_WORDS = 20;

// Builds a hook. `original` are the target's first 4 instructions.
// On success fills `patch` (4 words to write at target) and `trampoline`
// (*trampolineWords words to write at trampolineAddr). On failure returns
// false and sets *error.
bool BuildHook(const Word original[4], Word targetAddr, Word replacementAddr,
               Word patch[4], Word trampoline[MAX_TRAMPOLINE_WORDS], int *trampolineWords,
               const char **error);

} // namespace PPC
} // namespace ForgedLCE

#endif
