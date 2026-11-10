// 0.3.6: the CPU, the laptop power state and Wine / Proton, for the log and for
// two decisions: a weak CPU skips the 1440p step-down and gets the CPU-side cuts.
// The rules are pure; the readers use CPUID, kernel32 and ntdll only, so they are
// safe at DLL load.
#pragma once
#include <stddef.h>

namespace sys {

struct Cpu {
    char name[64];
    int  cores;    // physical cores, 0 when unknown
    int  threads;  // logical processors, 0 when unknown
};

// A CPU that holds F1 25 back: 4 threads or fewer, Intel Core i3/i5/i7 of the 7th
// generation or older, Core 2 / Pentium / Celeron, or AMD FX, Phenom, Athlon and
// the A-series APUs. why gets the reason (nullptr when not weak).
bool WeakCpu(const char* name, int cores, int threads, const char** why = nullptr);

struct Power {
    bool known;       // GetSystemPowerStatus answered
    bool hasBattery;  // a laptop (or a UPS that reports as a battery)
    bool onBattery;   // not plugged in
    bool saver;       // Windows battery saver on
    int  percent;     // battery charge, -1 when unknown
};

// "plugged in", "on battery (63%), battery saver on", "no battery (desktop)" ...
void PowerText(const Power& p, char* out, size_t cap);
// The log note for a state that costs fps, or nullptr.
const char* PowerNote(const Power& p);

Cpu ReadCpu();
Power ReadPower();
// The Wine version when running under Wine / Proton, else nullptr.
const char* WineVersion();

}  // namespace sys
