#pragma once

namespace WorldRPG::Game {

// Gives libGTASA its own heap inside one address window smaller than 2 GB.
//
// libGTASA 2.10 arm64 keeps RenderWare code that moves a block and then fixes pointers with a 32-bit
// difference (RxLockedPipeUnlock at 0x278e3c: `sub x9, x0, x21` then `add x8, x8, w9, sxtw`). That is only
// correct while every block the game compares or moves is less than 2 GB from the others. The system
// allocator places size classes in regions that can be further apart, and the layout is fixed per boot,
// so the same APK works on one boot and breaks on the next (lost geometry, or SIGSEGV in RwEngineStart).
//
// The call replaces malloc, calloc, realloc, free, strdup and operator new/delete in libGTASA's GOT.
// It must run after libGTASA is loaded and before its engine thread starts. Returns false when libGTASA
// is not loaded, the arena cannot be reserved, or the "noheap" debug switch is set.
bool InstallGameHeap();

// One log line with the arena use. Safe to call from any thread.
void LogGameHeap(const char* reason);

}  // namespace WorldRPG::Game
