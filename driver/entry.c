#include "hv7.h"

static h7_npt *g_npt = NULL;

static void h7_snapshot_guest(h7_guest_ctx *out)
{
    RtlZeroMemory(out, sizeof(*out));

    struct { USHORT limit; ULONG64 base; } gdtr = {0}, idtr = {0};
    h7_sgdt(&gdtr);
    __sidt(&idtr);

    out->gdtr_base  = gdtr.base;
    out->gdtr_limit = gdtr.limit;
    out->idtr_base  = idtr.base;
    out->idtr_limit = idtr.limit;

    out->cs = h7_read_cs();
    out->ds = h7_read_ds();
    out->es = h7_read_es();
    out->ss = h7_read_ss();

    out->cr0    = __readcr0();
    out->cr2    = __readcr2();
    out->cr3    = __readcr3();
    out->cr4    = __readcr4();
    out->efer   = __readmsr(MSR_EFER);
    out->gpat   = __readmsr(MSR_PAT);
    out->rflags = h7_read_rflags();
    out->rsp    = h7_read_rsp();
    out->rip    = h7_read_rip();
}

static BOOLEAN h7_already_live(void)
{
    int regs[4] = {0};
    __cpuid(regs, CPUID_HV_PING);
    return regs[2] == CPUID_PING_MAGIC;
}

static NTSTATUS h7_virtualize_cpu(void *ctx)
{
    h7_npt *tables = (h7_npt *)ctx;

    ULONG64 vmcr = __readmsr(MSR_VM_CR);
    if (vmcr & VM_CR_SVMDIS)
        __writemsr(MSR_VM_CR, vmcr & ~VM_CR_SVMDIS);

    ULONG64 efer = __readmsr(MSR_EFER);
    if (!(efer & EFER_SVME))
        __writemsr(MSR_EFER, efer | EFER_SVME);

    h7_vcpu *cpu = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(h7_vcpu), 'h7vc');
    if (!cpu) return STATUS_NO_MEMORY;
    RtlZeroMemory(cpu, sizeof(*cpu));

    h7_guest_ctx snap;
    h7_snapshot_guest(&snap);

    // CAPTURE_GUEST returns twice: first via snapshot, second after vmrun
    if (h7_already_live()) {
        H7_LOG("cpu %u virtualized", KeGetCurrentProcessorNumberEx(NULL));
        return STATUS_SUCCESS;
    }

    h7_fill_vmcb(cpu, &snap, tables);
    h7_launch_vm((ULONG64)&cpu->top.guest_vmcb_pa);

    KeBugCheck(MANUALLY_INITIATED_CRASH);
    return STATUS_UNSUCCESSFUL;
}

static NTSTATUS h7_devirt_cpu(void *ctx)
{
    UNREFERENCED_PARAMETER(ctx);
    int regs[4];
    __cpuidex(regs, CPUID_HV_UNLOAD, CPUID_HV_UNLOAD);

    if (regs[2] != CPUID_PING_MAGIC)
        return STATUS_SUCCESS;

    // vcpu pointer split across eax/edx by the exit handler
    ULONG64 hi = (ULONG64)(ULONG)regs[3];
    ULONG64 lo = (ULONG64)(ULONG)regs[0];
    h7_vcpu *cpu = (h7_vcpu *)(hi << 32 | lo);
    ExFreePoolWithTag(cpu, 'h7vc');

    H7_LOG("cpu %u devirtualized", KeGetCurrentProcessorNumberEx(NULL));
    return STATUS_SUCCESS;
}

typedef NTSTATUS (*h7_percpu_fn)(void *ctx);

typedef struct _h7_dpc_ctx {
    h7_percpu_fn fn;
    void        *arg;
    NTSTATUS    *results;
    volatile LONG done;
    KEVENT       ev;
    LONG         total;
} h7_dpc_ctx;

static void h7_dpc_routine(PKDPC dpc, void *deferred, void *sa1, void *sa2)
{
    UNREFERENCED_PARAMETER(dpc);
    h7_dpc_ctx *dc = (h7_dpc_ctx *)deferred;
    ULONG idx = KeGetCurrentProcessorNumberEx(NULL);

    dc->results[idx] = dc->fn(dc->arg);

    InterlockedIncrement(&dc->done);
    if (sa1 && sa2) {
        KeSignalCallDpcSynchronize(sa2);
        KeSignalCallDpcDone(sa1);
    }
    if (dc->done == dc->total)
        KeSetEvent(&dc->ev, 0, FALSE);
}

static NTSTATUS h7_run_on_all_cpus(h7_percpu_fn fn, void *arg)
{
    ULONG count = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);

    h7_dpc_ctx dc;
    dc.fn    = fn;
    dc.arg   = arg;
    dc.done  = 0;
    dc.total = (LONG)count;
    dc.results = ExAllocatePool2(POOL_FLAG_NON_PAGED, count * sizeof(NTSTATUS), 'h7dp');
    if (!dc.results) return STATUS_NO_MEMORY;
    KeInitializeEvent(&dc.ev, NotificationEvent, FALSE);

    KeGenericCallDpc(h7_dpc_routine, &dc);
    KeWaitForSingleObject(&dc.ev, Executive, KernelMode, FALSE, NULL);

    NTSTATUS final = STATUS_SUCCESS;
    for (ULONG i = 0; i < count; i++) {
        if (!NT_SUCCESS(dc.results[i])) {
            H7_LOG("cpu %u failed: 0x%08X", i, dc.results[i]);
            final = dc.results[i];
        }
    }
    ExFreePoolWithTag(dc.results, 'h7dp');
    return final;
}

NTSTATUS h7_go_live(h7_npt **out_npt)
{
    h7_npt *t = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(h7_npt), 'h7np');
    if (!t) return STATUS_NO_MEMORY;
    RtlZeroMemory(t, sizeof(*t));

    h7_build_npt(t);

    PHYSICAL_ADDRESS lo = {0}, hi = {0};
    hi.QuadPart = -1LL;
    t->msrpm = MmAllocateContiguousMemorySpecifyCache(MSRPM_SIZE, lo, hi, lo, MmCached);
    if (!t->msrpm) {
        ExFreePoolWithTag(t, 'h7np');
        return STATUS_NO_MEMORY;
    }
    h7_build_msrpm(t->msrpm);

    NTSTATUS st = h7_run_on_all_cpus(h7_virtualize_cpu, t);
    if (!NT_SUCCESS(st)) {
        MmFreeContiguousMemory(t->msrpm);
        ExFreePoolWithTag(t, 'h7np');
        return st;
    }

    *out_npt = t;
    return STATUS_SUCCESS;
}

NTSTATUS h7_shutdown(void)
{
    if (!g_npt) return STATUS_SUCCESS;

    NTSTATUS st = h7_run_on_all_cpus(h7_devirt_cpu, NULL);

    h7_hook_cleanup();

    if (g_npt->msrpm)
        MmFreeContiguousMemory(g_npt->msrpm);
    ExFreePoolWithTag(g_npt, 'h7np');
    g_npt = NULL;
    return st;
}

h7_npt *h7_get_npt(void)
{
    return g_npt;
}

static void h7_unload(PDRIVER_OBJECT drv)
{
    h7_io_cleanup(drv);
    h7_shutdown();
    H7_LOG("unloaded");
}

NTSTATUS DriverEntry(PDRIVER_OBJECT drv, PUNICODE_STRING reg)
{
    UNREFERENCED_PARAMETER(reg);
    H7_LOG("entry");

    ExInitializeDriverRuntime(DrvRtPoolNxOptIn);
    drv->DriverUnload = h7_unload;

    NTSTATUS st = h7_check_svm_support();
    if (!NT_SUCCESS(st)) {
        H7_LOG("svm not supported (0x%08X)", st);
        return st;
    }

    st = h7_go_live(&g_npt);
    if (!NT_SUCCESS(st)) {
        H7_LOG("go_live failed (0x%08X)", st);
        return st;
    }

    st = h7_io_init(drv);
    if (!NT_SUCCESS(st)) {
        H7_LOG("io init failed (0x%08X)", st);
        h7_shutdown();
        return st;
    }

    H7_LOG("running");
    return STATUS_SUCCESS;
}
