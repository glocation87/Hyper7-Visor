#pragma once
#include <ntddk.h>
#include <intrin.h>
#include "arch.h"
#include "vmcb.h"
#include "npt.h"
#include "vcpu.h"
#include "h7_abi.h"

void __stdcall h7_launch_vm(ULONG64 host_rsp);
USHORT __stdcall h7_read_cs(void);
USHORT __stdcall h7_read_ss(void);
USHORT __stdcall h7_read_ds(void);
USHORT __stdcall h7_read_es(void);
ULONG64 __stdcall h7_read_rflags(void);
ULONG64 __stdcall h7_read_rsp(void);
ULONG64 __stdcall h7_read_rip(void);
void h7_sgdt(void *out);

BOOLEAN __stdcall h7_handle_exit(h7_vcpu *cpu, h7_gp_regs *gprs);

NTSTATUS h7_check_svm_support(void);
void     h7_build_npt(h7_npt *tables);
void     h7_build_msrpm(void *bitmap);
void     h7_fill_vmcb(h7_vcpu *cpu, h7_guest_ctx *ctx, h7_npt *tables);
NTSTATUS h7_go_live(h7_npt **out_npt);
NTSTATUS h7_shutdown(void);
h7_npt  *h7_get_npt(void);
USHORT   h7_seg_attrib(ULONG64 gdt_base, USHORT selector);
h7_stats *h7_get_stats(void);

NTSTATUS h7_io_init(PDRIVER_OBJECT drv);
void     h7_io_cleanup(PDRIVER_OBJECT drv);

ULONG64 h7_hypercall(ULONG64 num, ULONG64 a1, ULONG64 a2, ULONG64 a3);

#define H7_LOG(fmt, ...) do {                           \
    if (!KD_DEBUGGER_NOT_PRESENT)                       \
        DbgPrint("[h7] " fmt "\n", ##__VA_ARGS__);      \
} while (0)

NTKERNELAPI void KeGenericCallDpc(PKDEFERRED_ROUTINE, void *);
NTKERNELAPI void KeSignalCallDpcDone(void *);
NTKERNELAPI LOGICAL KeSignalCallDpcSynchronize(void *);
NTKERNELAPI PVOID MmGetVirtualForPhysical(PHYSICAL_ADDRESS);
