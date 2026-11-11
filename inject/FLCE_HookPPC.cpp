// ForgedLCE: PowerPC (Xbox 360) function-hook builder. See FLCE_HookPPC.h.
#include "FLCE_HookPPC.h"

namespace ForgedLCE {
namespace PPC {

namespace {

const Word R11 = 11;

Word Opcode(Word insn) { return insn >> 26; }

// Target of an I-form branch (b / bl / ba / bla) at `addr`.
Word BranchTarget(Word insn, Word addr)
{
    Word li = insn & 0x03FFFFFC;
    if (li & 0x02000000) li |= 0xFC000000; // sign-extend the 26-bit displacement
    bool absolute = (insn & 2) != 0;
    return absolute ? li : addr + li;
}

} // namespace

void EncodeJump(Word dest, Word out[4], bool link)
{
    out[0] = 0x3C000000 | (R11 << 21) | (dest >> 16);              // lis   r11, hi
    out[1] = 0x60000000 | (R11 << 21) | (R11 << 16) | (dest & 0xFFFF); // ori r11, r11, lo
    out[2] = 0x7C0903A6 | (R11 << 21);                             // mtctr r11
    out[3] = link ? BCTRL : BCTR;
}

bool BuildHook(const Word original[4], Word targetAddr, Word replacementAddr,
               Word patch[4], Word trampoline[MAX_TRAMPOLINE_WORDS], int *trampolineWords,
               const char **error)
{
    int n = 0;
    for (int i = 0; i < 4; ++i) {
        Word insn = original[i];
        Word addr = targetAddr + 4 * (Word)i;
        Word op = Opcode(insn);

        if (op == 18) {                      // b / bl / ba / bla
            Word dest = BranchTarget(insn, addr);
            bool link = (insn & 1) != 0;
            EncodeJump(dest, trampoline + n, link);
            n += 4;
            if (!link) {
                // An unconditional jump ends the copied code: the jump back
                // to target + 16 would never be reached.
                if (i != 3) {
                    *error = "an unconditional branch in the first 3 instructions (function is too short to hook)";
                    return false;
                }
                *trampolineWords = n;
                EncodeJump(replacementAddr, patch);
                return true;
            }
            continue;
        }
        if (op == 16 && (insn & 2) == 0) {   // relative conditional branch
            *error = "a relative conditional branch in the first 4 instructions";
            return false;
        }
        trampoline[n++] = insn;
    }
    EncodeJump(targetAddr + 16, trampoline + n);
    n += 4;
    *trampolineWords = n;
    EncodeJump(replacementAddr, patch);
    return true;
}

} // namespace PPC
} // namespace ForgedLCE
