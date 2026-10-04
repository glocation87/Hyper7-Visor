#pragma once
#include <ntddk.h>
#include <intrin.h>
#include "arch.h"
#include "vmcb.h"
#include "npt.h"
#include "vcpu.h"

/* asm exports */
void __stdcall h7_launch_vm(ULONG64 host_rsp);
USHORT __stdcall h7_read_cs(void);
USHORT __stdcall h7_read_ss(void);
USHORT __stdcall h7_read_ds(void);
USHORT __stdcall h7_read_es(void);
ULONG64 __stdcall h7_read_rflags(void);
ULONG64 __stdcall h7_read_rsp(void);
ULONG64 __stdcall h7_read_rip(void);
void h7_sgdt(void *out);

/* vmexit dispatch (called from asm) */
BOOLEAN __stdcall h7_handle_exit(h7_vcpu *cpu, h7_gp_regs *gprs);

/* core init / teardown */
NTSTATUS h7_check_svm_support(void);
void     h7_build_npt(h7_npt *tables);
void     h7_build_msrpm(void *bitmap);
void     h7_fill_vmcb(h7_vcpu *cpu, h7_guest_ctx *ctx, h7_npt *tables);
NTSTATUS h7_go_live(h7_npt **out_npt);
NTSTATUS h7_shutdown(void);

/* segment attribute helper */
USHORT h7_seg_attrib(ULONG64 gdt_base, USHORT selector);

/* dbg print — only when debugger present */
#define H7_LOG(fmt, ...) do {          \
    if (!KD_DEBUGGER_NOT_PRESENT)      \
        DbgPrint("[h7] " fmt "\n", ##__VA_ARGS__); \
} while (0)

NTKERNELAPI void KeGenericCallDpc(PKDEFERRED_ROUTINE, void *);
NTKERNELAPI void KeSignalCallDpcDone(void *);
NTKERNELAPI LOGICAL KeSignalCallDpcSynchronize(void *);
