#pragma once
#include <ntddk.h>

#define NPT_TABLE_ENTRIES 512

typedef struct _h7_pml4e {
    union {
        ULONG64 val;
        struct {
            ULONG64 present    : 1;
            ULONG64 write      : 1;
            ULONG64 user       : 1;
            ULONG64 pwt        : 1;
            ULONG64 pcd        : 1;
            ULONG64 accessed   : 1;
            ULONG64 ign0       : 3;
            ULONG64 avl        : 3;
            ULONG64 pfn        : 40;
            ULONG64 reserved   : 11;
            ULONG64 nx         : 1;
        };
    };
} h7_pml4e;

typedef h7_pml4e h7_pdpe; /* same layout for pointer entries */

typedef struct _h7_pde_2mb {
    union {
        ULONG64 val;
        struct {
            ULONG64 present    : 1;
            ULONG64 write      : 1;
            ULONG64 user       : 1;
            ULONG64 pwt        : 1;
            ULONG64 pcd        : 1;
            ULONG64 accessed   : 1;
            ULONG64 dirty      : 1;
            ULONG64 large_page : 1;
            ULONG64 global     : 1;
            ULONG64 avl        : 3;
            ULONG64 pat        : 1;
            ULONG64 reserved0  : 8;
            ULONG64 pfn        : 31;
            ULONG64 reserved1  : 11;
            ULONG64 nx         : 1;
        };
    };
} h7_pde_2mb;

/* shared across all vcpus: the identity-mapped nested page tables + msr bitmap */
typedef struct _h7_npt {
    void *msrpm;
    DECLSPEC_ALIGN(PAGE_SIZE) h7_pml4e   pml4[NPT_TABLE_ENTRIES];
    DECLSPEC_ALIGN(PAGE_SIZE) h7_pdpe    pdp[NPT_TABLE_ENTRIES];
    DECLSPEC_ALIGN(PAGE_SIZE) h7_pde_2mb pd[NPT_TABLE_ENTRIES][NPT_TABLE_ENTRIES];
} h7_npt;
