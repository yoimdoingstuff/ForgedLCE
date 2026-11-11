// What the built-in test mod (TestMod.cpp) observed, read by run_tests.cpp.
#ifndef FLCE_TESTMODSTATE_H
#define FLCE_TESTMODSTATE_H

#include "forgedlce/ForgedLCE.h"
#include <string>

struct TestModState {
    const FLCE_Api *api;

    // preInit
    int  rejectedRegistrations;  // invalid names, bad structSize, duplicates
    int  blockA;                 // testmod:block_a
    int  itemA;                  // testmod:item_a
    int  findIdOk;               // findId found block_a and not a missing name
    int  recipeInPreInit;        // must be 0 (wrong phase)
    int  badEventTypeSubscribe;  // must be 0

    // init
    int  registerInInit;         // must be -1 (wrong phase)
    int  shapedOk, shapedBadRows, shapedMissingKey, shapedUnknownId;
    int  shapelessOk, smeltingOk, smeltingUnknown;

    // postInit
    int  exampleBlockSeenInPostInit; // examplemod's block id via findId

    // events
    std::string blockBreakOrder; // handler letters in call order
    int  lowestSawCanceled;      // 1 if the LOWEST handler saw canceled==1
    int  serverTickEnds;
    std::string clientTickPhases; // "S"/"E"
    int  entityJoins;
    int  lastJoinIsPlayer;

    TestModState()
        : api(0), rejectedRegistrations(0), blockA(-1), itemA(-1), findIdOk(0), recipeInPreInit(-1),
          badEventTypeSubscribe(-1), registerInInit(0), shapedOk(-1), shapedBadRows(-1),
          shapedMissingKey(-1), shapedUnknownId(-1), shapelessOk(-1), smeltingOk(-1), smeltingUnknown(-1),
          exampleBlockSeenInPostInit(-1), lowestSawCanceled(0), serverTickEnds(0), entityJoins(0),
          lastJoinIsPlayer(-1) {}
};

extern TestModState g_testModState;

#endif
