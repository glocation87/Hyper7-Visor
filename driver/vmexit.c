#include "hv7.h"

#pragma intrinsic(__rdtsc, __readmsr, __writemsr, __cpuidex)

static h7_stats g_stats = {0};

h7_stats *h7_get_stats(void) { return &g_stats; }

static void inject_ud(h7_vcpu *cpu)
{
    cpu->guest_vmcb.control.event_inject = (1ULL << 31) | (3ULL << 8) | 6;
}

static void on_cpuid(h7_vcpu *cpu, h7_exit_ctx *ectx)
{
    int leaf = (int)ectx->gprs->rax;
    int sub  = (int)ectx->gprs->rcx;
    int regs[4] = {0};

    __cpuidex(regs, leaf, sub);

    switch (leaf) {
    case CPUID_FEATURES:
        regs[2] |= FEAT_ECX_HYPERVISOR;
        break;
    case CPUID_HV_VENDOR:
        regs[0] = CPUID_HV_INTERFACE;
        regs[1] = 'pyH';
        regs[2] = '7re';
        regs[3] = 'osiV';
        break;
    case CPUID_HV_INTERFACE:
        regs[0] = '0#vH';
        regs[1] = regs[2] = regs[3] = 0;
        break;
    case CPUID_HV_PING:
        regs[2] = CPUID_PING_MAGIC;
        break;
    case CPUID_HV_UNLOAD:
        if (sub == CPUID_HV_UNLOAD) {
            // only let ring 0 request devirt
            USHORT dpl = cpu->guest_vmcb.save.ss.attrib & 0x60;
            if (dpl == 0)
                ectx->wants_off = TRUE;
        }
        break;
    }

    ectx->gprs->rax = regs[0];
    ectx->gprs->rbx = regs[1];
    ectx->gprs->rcx = regs[2];
    ectx->gprs->rdx = regs[3];

    cpu->guest_vmcb.save.rip = cpu->guest_vmcb.control.nrip;
}

static void on_msr(h7_vcpu *cpu, h7_exit_ctx *ectx)
{
    ULONG msr = (ULONG)(ectx->gprs->rcx & 0xFFFFFFFF);
    BOOLEAN is_write = cpu->guest_vmcb.control.exit_info1 != 0;

    if (is_write) {
        ULARGE_INTEGER v;
        v.LowPart  = (ULONG)(ectx->gprs->rax & 0xFFFFFFFF);
        v.HighPart = (ULONG)(ectx->gprs->rdx & 0xFFFFFFFF);

        if (msr == MSR_EFER) {
            // guest can't clear SVME while we're running on top of it
            v.QuadPart |= EFER_SVME;
            cpu->guest_vmcb.save.efer = v.QuadPart;
        }
        __writemsr(msr, v.QuadPart);
    } else {
        ULARGE_INTEGER v;
        v.QuadPart = __readmsr(msr);
        ectx->gprs->rax = v.LowPart;
        ectx->gprs->rdx = v.HighPart;
    }
    cpu->guest_vmcb.save.rip = cpu->guest_vmcb.control.nrip;
}

static void on_rdtsc(h7_vcpu *cpu, h7_exit_ctx *ectx)
{
    ULONG64 tsc = __rdtsc() + cpu->tsc_offset;
    ectx->gprs->rax = (ULONG)tsc;
    ectx->gprs->rdx = (ULONG)(tsc >> 32);
    cpu->guest_vmcb.save.rip = cpu->guest_vmcb.control.nrip;
}

static ULONG64 do_hypercall(h7_vcpu *cpu, h7_gp_regs *gprs)
{
    ULONG64 num = gprs->rax;
    ULONG64 a1  = gprs->rcx;
    ULONG64 a2  = gprs->rdx;
    ULONG64 a3  = gprs->r8;

    switch (num) {
    case HC_PING:
        return CPUID_PING_MAGIC;

    case HC_GET_STATS:
        return (ULONG64)&g_stats;

    case HC_GET_CPU:
        return (ULONG64)cpu;

    case HC_SET_TSC_OFFSET:
        cpu->tsc_offset = a1;
        return 0;

    case HC_READ_PHYS: {
        PHYSICAL_ADDRESS pa; pa.QuadPart = a1;
        void *va = MmGetVirtualForPhysical(pa);
        if (!va) return (ULONG64)-1;
        return *(ULONG64 *)va;
    }

    default:
        return (ULONG64)-1;
    }
}

static void on_vmmcall(h7_vcpu *cpu, h7_exit_ctx *ectx)
{
    // only trust ring 0 callers
    USHORT dpl = cpu->guest_vmcb.save.ss.attrib & 0x60;
    if (dpl != 0) {
        inject_ud(cpu);
        return;
    }

    ectx->gprs->rax = do_hypercall(cpu, ectx->gprs);
    cpu->guest_vmcb.save.rip = cpu->guest_vmcb.control.nrip;
}

static void bump(volatile LONG64 *counter) { InterlockedIncrement64(counter); }

BOOLEAN __stdcall h7_handle_exit(h7_vcpu *cpu, h7_gp_regs *gprs)
{
    h7_exit_ctx ectx = { .gprs = gprs, .wants_off = FALSE };

    __svm_vmload(cpu->top.host_vmcb_pa);
    gprs->rax = cpu->guest_vmcb.save.rax;

    ULONG64 code = cpu->guest_vmcb.control.exit_code;
    bump(&g_stats.total);

    switch (code) {
    case VMEXIT_CPUID:   bump(&g_stats.cpuid);   on_cpuid(cpu, &ectx); break;
    case VMEXIT_MSR:     bump(&g_stats.msr);     on_msr(cpu, &ectx); break;
    case VMEXIT_RDTSC:   bump(&g_stats.rdtsc);   on_rdtsc(cpu, &ectx); break;
    case VMEXIT_VMMCALL: bump(&g_stats.vmmcall); on_vmmcall(cpu, &ectx); break;

    case VMEXIT_VMRUN:
    case VMEXIT_VMLOAD:
    case VMEXIT_VMSAVE:
    case VMEXIT_CLGI:
    case VMEXIT_STGI:
    case VMEXIT_SKINIT:
        bump(&g_stats.injected_ud);
        inject_ud(cpu);
        break;

    default:
        H7_LOG("unhandled exit 0x%llx rip=%llx", code, cpu->guest_vmcb.save.rip);
        KeBugCheckEx(0xDEAD7777UL, cpu->guest_vmcb.save.rip, code, 0, 0);
    }

    if (ectx.wants_off) {
        // asm bail-out expects: eax/edx = &cpu split, rbx = nrip, rcx = guest rsp
        gprs->rax = (ULONG64)cpu & 0xFFFFFFFF;
        gprs->rbx = cpu->guest_vmcb.control.nrip;
        gprs->rcx = cpu->guest_vmcb.save.rsp;
        gprs->rdx = (ULONG64)cpu >> 32;

        __svm_vmload(cpu->top.guest_vmcb_pa);
        _disable();
        __svm_stgi();
        __writemsr(MSR_EFER, __readmsr(MSR_EFER) & ~EFER_SVME);
        __writeeflags(cpu->guest_vmcb.save.rflags);
    } else {
        cpu->guest_vmcb.save.rax = gprs->rax;
    }
    return ectx.wants_off;
}
