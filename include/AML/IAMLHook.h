#pragma once
#include <stdint.h>

class IAMLHook {
public:
    virtual int Hook(void* addr, void* hook, void** orig) = 0;
    virtual int Unhook(void* addr) = 0;
};
