#pragma once

// hypercall numbers (passed in rax to vmmcall, ring-0 only)
#define HC_PING             0x7701
#define HC_GET_STATS        0x7702
#define HC_GET_CPU          0x7703
#define HC_SET_TSC_OFFSET   0x7704
#define HC_READ_PHYS        0x7705

// device + ioctls
#define H7_DEVICE_NAME_W    L"\\Device\\hv7"
#define H7_SYMLINK_W        L"\\DosDevices\\hv7"
#define H7_DOS_PATH         "\\\\.\\hv7"

#ifndef CTL_CODE
// user-mode build still needs the macro
#define FILE_DEVICE_UNKNOWN  0x00000022
#define METHOD_BUFFERED      0
#define FILE_ANY_ACCESS      0
#define CTL_CODE(dev, fn, m, acc) \
    (((dev) << 16) | ((acc) << 14) | ((fn) << 2) | (m))
#endif

#define IOCTL_H7_PING       CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_H7_STATS      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_H7_UNLOAD     CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _h7_stats_out {
    unsigned __int64 total;
    unsigned __int64 cpuid;
    unsigned __int64 msr;
    unsigned __int64 rdtsc;
    unsigned __int64 vmmcall;
    unsigned __int64 injected_ud;
} h7_stats_out;
