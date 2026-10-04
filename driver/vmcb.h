#pragma once
#include <ntddk.h>

#pragma pack(push, 1)

typedef struct _h7_seg {
    USHORT selector;
    USHORT attrib;
    ULONG  limit;
    ULONG64 base;
} h7_seg;

typedef struct _h7_vmcb_ctl {
    USHORT  intercept_cr_read;
    USHORT  intercept_cr_write;
    USHORT  intercept_dr_read;
    USHORT  intercept_dr_write;
    ULONG   intercept_exceptions;
    ULONG   intercept_misc1;
    ULONG   intercept_misc2;
    ULONG   intercept_misc3;
    UCHAR   reserved1[0x3C - 0x18];
    USHORT  pause_filter_threshold;
    USHORT  pause_filter_count;
    ULONG64 iopm_base_pa;
    ULONG64 msrpm_base_pa;
    ULONG64 tsc_offset;
    ULONG   guest_asid;
    UCHAR   tlb_control;
    UCHAR   reserved2[3];
    ULONG64 vintr;
    ULONG64 intr_shadow;
    ULONG64 exit_code;
    ULONG64 exit_info1;
    ULONG64 exit_info2;
    ULONG64 exit_int_info;
    ULONG64 np_enable;
    ULONG64 avic_bar;
    ULONG64 ghcb_pa;
    ULONG64 event_inject;
    ULONG64 ncr3;
    ULONG64 lbr_virt;
    ULONG64 vmcb_clean;
    ULONG64 nrip;
    UCHAR   fetch_count;
    UCHAR   fetch_bytes[15];
    UCHAR   reserved3[0x400 - 0xE0];
} h7_vmcb_ctl;
static_assert(sizeof(h7_vmcb_ctl) == 0x400, "control area size");

typedef struct _h7_vmcb_state {
    h7_seg es;
    h7_seg cs;
    h7_seg ss;
    h7_seg ds;
    h7_seg fs;
    h7_seg gs;
    h7_seg gdtr;
    h7_seg ldtr;
    h7_seg idtr;
    h7_seg tr;
    UCHAR   reserved1[0xCB - 0xA0];
    UCHAR   cpl;
    ULONG   reserved2;
    ULONG64 efer;
    UCHAR   reserved3[0x148 - 0xD8];
    ULONG64 cr4;
    ULONG64 cr3;
    ULONG64 cr0;
    ULONG64 dr7;
    ULONG64 dr6;
    ULONG64 rflags;
    ULONG64 rip;
    UCHAR   reserved4[0x1D8 - 0x180];
    ULONG64 rsp;
    ULONG64 scet;
    ULONG64 ssp;
    ULONG64 isst;
    ULONG64 rax;
    ULONG64 star;
    ULONG64 lstar;
    ULONG64 cstar;
    ULONG64 sfmask;
    ULONG64 kernel_gs_base;
    ULONG64 sysenter_cs;
    ULONG64 sysenter_esp;
    ULONG64 sysenter_eip;
    ULONG64 cr2;
    UCHAR   reserved5[0x268 - 0x248];
    ULONG64 gpat;
    UCHAR   reserved6[0xC00 - 0x270];
} h7_vmcb_state;
static_assert(sizeof(h7_vmcb_state) == 0xC00, "save area size");

typedef struct DECLSPEC_ALIGN(PAGE_SIZE) _h7_vmcb {
    h7_vmcb_ctl   control;
    h7_vmcb_state save;
} h7_vmcb;
static_assert(sizeof(h7_vmcb) == PAGE_SIZE, "vmcb page");

#pragma pack(pop)
