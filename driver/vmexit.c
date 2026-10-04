#include "hv7.h"

#pragma intrinsic(__rdtsc, __readmsr, __writemsr, __cpuidex)

static void inject_ud(h7_vcpu *cpu)
{
    /* type=3 (hw exception), vector=6 (#UD), valid=1 */
    cpu->guest_vmcb.control.event_inject =
        (1ULL << 31) | (3ULL << 8) | 6;
}

static void inject_gp(h7_vcpu *cpu)
{
    /* type=3, vector=13 (#GP), valid=1, error_code_valid=1, error=0 */
    cpu->guest_vmcb.control.event_inject =
        (1ULL << 31) | (1ULL << 11) | (3ULL << 8) | 13;
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
        regs[0] = CPUID_HV_INTERFACE; /* max leaf */
        regs[1] = 'pyH';   /* "Hyp" */
        regs[2] = '7re';   /* "er7" */
        regs[3] = 'osiV';  /* "Viso" */
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
            USHORT dpl = cpu->guest_vmcb.save.ss.attrib & 0x60;
            if (dpl == 0) /* ring 0 only */
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
            /* never let guest clear SVME while we're live */
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
    ULONG64 tsc = __rdtsc();
    ectx->gprs->rax = (ULONG)(tsc);
    ectx->gprs->rdx = (ULONG)(tsc >> 32);
    cpu->guest_vmcb.save.rip = cpu->guest_vmcb.control.nrip;
}

static void on_vmmcall(h7_vcpu *cpu, h7_exit_ctx *ectx)
{
    UNREFERENCED_PARAMETER(ectx);
    cpu->guest_vmcb.save.rip = cpu->guest_vmcb.control.nrip;
}

BOOLEAN __stdcall h7_handle_exit(h7_vcpu *cpu, h7_gp_regs *gprs)
{
    h7_exit_ctx ectx;
    ectx.gprs      = gprs;
    ectx.wants_off = FALSE;

    __svm_vmload(cpu->top.host_vmcb_pa);
    gprs->rax = cpu->guest_vmcb.save.rax;

    switch (cpu->guest_vmcb.control.exit_code) {
    case VMEXIT_CPUID:
        on_cpuid(cpu, &ectx);
        break;
    case VMEXIT_MSR:
        on_msr(cpu, &ectx);
        break;
    case VMEXIT_VMRUN:
    case VMEXIT_VMLOAD:
    case VMEXIT_VMSAVE:
    case VMEXIT_CLGI:
    case VMEXIT_STGI:
    case VMEXIT_SKINIT:
        inject_ud(cpu);
        break;
    case VMEXIT_VMMCALL:
        on_vmmcall(cpu, &ectx);
        break;
    case VMEXIT_RDTSC:
        on_rdtsc(cpu, &ectx);
        break;
    default:
        H7_LOG("unhandled exit 0x%llx at rip %llx",
               cpu->guest_vmcb.control.exit_code,
               cpu->guest_vmcb.save.rip);
        KeBugCheckEx(0xDEAD7777UL,
                     cpu->guest_vmcb.save.rip,
                     cpu->guest_vmcb.control.exit_code, 0, 0);
        break;
    }

    if (ectx.wants_off) {
        /* set up for the asm bail-out path */
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
