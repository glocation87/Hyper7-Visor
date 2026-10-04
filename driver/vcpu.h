#pragma once
#include "vmcb.h"
#include "npt.h"

#define H7_STACK_SIZE  (KERNEL_STACK_SIZE)
#define H7_SENTINEL    0xDEADBEEFCAFE7777ULL

typedef struct _h7_gp_regs {
    ULONG64 r15, r14, r13, r12, r11, r10, r9, r8;
    ULONG64 rdi, rsi, rbp;
    ULONG64 rsp_dummy;
    ULONG64 rbx, rdx, rcx, rax;
} h7_gp_regs;

typedef struct _h7_stack_layout {
    ULONG64      guest_vmcb_pa;
    ULONG64      host_vmcb_pa;
    struct _h7_vcpu *self;
    h7_npt       *npt;
    ULONG64      sentinel;
} h7_stack_layout;

typedef struct DECLSPEC_ALIGN(PAGE_SIZE) _h7_vcpu {
    union {
        UCHAR host_stack_raw[H7_STACK_SIZE];
        struct {
            UCHAR _fill[H7_STACK_SIZE - sizeof(h7_stack_layout)];
            h7_stack_layout top;
        };
    };
    DECLSPEC_ALIGN(PAGE_SIZE) h7_vmcb guest_vmcb;
    DECLSPEC_ALIGN(PAGE_SIZE) h7_vmcb host_vmcb;
    DECLSPEC_ALIGN(PAGE_SIZE) UCHAR   host_save_area[PAGE_SIZE];
    ULONG64 tsc_offset;
} h7_vcpu;

typedef struct _h7_guest_ctx {
    ULONG64 gdtr_base;
    USHORT  gdtr_limit;
    ULONG64 idtr_base;
    USHORT  idtr_limit;
    USHORT  cs, ds, es, ss;
    ULONG64 cr0, cr2, cr3, cr4;
    ULONG64 efer, gpat;
    ULONG64 rflags, rsp, rip;
} h7_guest_ctx;

typedef struct _h7_exit_ctx {
    h7_gp_regs *gprs;
    BOOLEAN    wants_off;
} h7_exit_ctx;

typedef struct _h7_stats {
    volatile LONG64 total;
    volatile LONG64 cpuid;
    volatile LONG64 msr;
    volatile LONG64 rdtsc;
    volatile LONG64 vmmcall;
    volatile LONG64 injected_ud;
} h7_stats;
