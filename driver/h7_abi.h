#pragma once

// hypercall numbers (passed in rax to vmmcall, ring-0 only)
#define HC_PING             0x7701
#define HC_GET_STATS        0x7702
#define HC_GET_CPU          0x7703
#define HC_SET_TSC_OFFSET   0x7704
#define HC_READ_PHYS        0x7705
#define HC_CR3_WATCH        0x7706
#define HC_CR3_SAMPLE       0x7707
#define HC_LSTAR_READ       0x7708
#define HC_LSTAR_SHIM       0x7709
#define HC_LSTAR_HISTORY    0x770A
#define HC_READ_GVA         0x770B
#define HC_WRITE_GVA        0x770C
#define HC_HOOK_INSTALL     0x770D
#define HC_HOOK_REMOVE      0x770E
#define HC_HOOK_LIST        0x770F

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
#define IOCTL_H7_CR3_WATCH  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_H7_CR3_SAMPLE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_H7_LSTAR      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x805, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_H7_READ_GVA   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x806, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_H7_HOOK_ADD   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x807, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_H7_HOOK_LIST  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x808, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _h7_read_gva_req {
    unsigned __int64 cr3;      // 0 = use last-observed cr3
    unsigned __int64 gva;
    unsigned __int32 len;      // bytes to read, max 4096
} h7_read_gva_req;

typedef struct _h7_hook_req {
    unsigned __int64 target_gpa;      // 4KB-aligned
    unsigned char    patched[4096];   // bytes guest should see when fetching
} h7_hook_req;

typedef struct _h7_hook_info {
    unsigned __int64 target_gpa;
    unsigned __int64 shadow_pa;
    unsigned __int32 active;
} h7_hook_info;

typedef struct _h7_stats_out {
    unsigned __int64 total;
    unsigned __int64 cpuid;
    unsigned __int64 msr;
    unsigned __int64 rdtsc;
    unsigned __int64 rdtscp;
    unsigned __int64 vmmcall;
    unsigned __int64 cr3_write;
    unsigned __int64 npf;
    unsigned __int64 injected_ud;
} h7_stats_out;
