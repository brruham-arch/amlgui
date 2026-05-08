#pragma once
#include <stdint.h>

class IAMI {
public:
    virtual void* GetLib(const char* name) = 0;
    virtual uintptr_t GetSym(void* lib, const char* sym) = 0;
};
