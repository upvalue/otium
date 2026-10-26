#ifndef OTIUM_PROTOTYPE_FOREIGN_H
#define OTIUM_PROTOTYPE_FOREIGN_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*ForeignCallback)(void);
typedef union {
    int64_t signed_integer;
    uint64_t unsigned_integer;
    float single;
    double real;
    bool boolean;
    void *pointer;
    ForeignCallback callback;
} ForeignSlot;
typedef void (*ForeignAdapter)(void *target, const ForeignSlot *arguments, ForeignSlot *result);

#endif
