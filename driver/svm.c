#include "hv7.h"

NTSTATUS h7_check_svm_support(void)
{
    int regs[4];

    __cpuid(regs, CPUID_VENDOR);
    if (regs[1] != 'htuA' || regs[3] != 'itne' || regs[2] != 'DMAc')
        return STATUS_HV_INVALID_DEVICE_ID;

    __cpuid(regs, CPUID_EXT_FEATURES);
    if (!(regs[2] & EXT_ECX_SVM))
        return STATUS_HV_CPUID_FEATURE_VALIDATION_ERROR;
    if (!(regs[3] & EXT_EDX_LONG_MODE))
        return STATUS_HV_CPUID_FEATURE_VALIDATION_ERROR;

    __cpuid(regs, CPUID_SVM_FEATURES);
    if (!(regs[3] & SVM_EDX_NP))
        return STATUS_HV_CPUID_FEATURE_VALIDATION_ERROR;

    ULONG64 vmcr = __readmsr(MSR_VM_CR);
    if (vmcr & VM_CR_SVMDIS)
        return STATUS_HV_INVALID_DEVICE_STATE;

    return STATUS_SUCCESS;
}

void h7_build_npt(h7_npt *t)
{
    RtlZeroMemory(t->pml4, sizeof(t->pml4));
    RtlZeroMemory(t->pdp,  sizeof(t->pdp));

    ULONG64 pdp_pa = MmGetPhysicalAddress(&t->pdp[0]).QuadPart;
    t->pml4[0].val = 0;
    t->pml4[0].present = 1;
    t->pml4[0].write   = 1;
    t->pml4[0].user    = 1;
    t->pml4[0].pfn     = pdp_pa >> PAGE_SHIFT;

    for (ULONG i = 0; i < NPT_TABLE_ENTRIES; i++) {
        ULONG64 pd_pa = MmGetPhysicalAddress(&t->pd[i][0]).QuadPart;
        t->pdp[i].val = 0;
        t->pdp[i].present = 1;
        t->pdp[i].write   = 1;
        t->pdp[i].user    = 1;
        t->pdp[i].pfn     = pd_pa >> PAGE_SHIFT;

        for (ULONG j = 0; j < NPT_TABLE_ENTRIES; j++) {
            ULONG64 frame = ((ULONG64)i << 9) | j;
            t->pd[i][j].val = 0;
            t->pd[i][j].present    = 1;
            t->pd[i][j].write      = 1;
            t->pd[i][j].user       = 1;
            t->pd[i][j].large_page = 1;
            t->pd[i][j].pfn        = (ULONG)frame;
        }
    }
}

// bit for an MSR in range 0xC0000000-0xC0001FFF: bit 0 = read, bit 1 = write
static ULONG msrpm_bit(ULONG msr, BOOLEAN write)
{
    ULONG off = (msr - 0xC0000000) * 2;
    return (0x800 * 8) + off + (write ? 1 : 0);
}

void h7_build_msrpm(void *map)
{
    RtlZeroMemory(map, MSRPM_SIZE);

    RTL_BITMAP bm;
    RtlInitializeBitMap(&bm, (PULONG)map, MSRPM_SIZE * 8);

    RtlSetBit(&bm, msrpm_bit(MSR_EFER,  TRUE));   // guest can't clear SVME
    RtlSetBit(&bm, msrpm_bit(MSR_LSTAR, TRUE));   // trace syscall handler installs
    RtlSetBit(&bm, msrpm_bit(MSR_LSTAR, FALSE));  // and reads (so our shadow is seen)
}

USHORT h7_seg_attrib(ULONG64 gdt_base, USHORT sel)
{
    if (sel == 0 || (sel >> 3) == 0)
        return 0;

    typedef struct { USHORT lo; USHORT mid; UCHAR base_hi; UCHAR flags; UCHAR flags2; UCHAR base_top; } GDT_RAW;
    GDT_RAW *ent = (GDT_RAW *)(gdt_base + (sel & ~7));

    // AMD packs attribs as low byte = flags, high nibble = flags2>>4
    USHORT lo4 = ent->flags & 0xFF;
    USHORT hi4 = (ent->flags2 >> 4) & 0x0F;
    return (hi4 << 8) | lo4;
}

void h7_fill_vmcb(h7_vcpu *cpu, h7_guest_ctx *ctx, h7_npt *tables)
{
    h7_vmcb_ctl   *ctl = &cpu->guest_vmcb.control;
    h7_vmcb_state *st  = &cpu->guest_vmcb.save;

    RtlZeroMemory(ctl, sizeof(*ctl));
    RtlZeroMemory(st,  sizeof(*st));

    ctl->intercept_cr_write |= (1u << 3);                             // CR3 write
    ctl->intercept_misc1    |= (1u << 18) | (1u << 15) | (1u << 14);  // cpuid, msr, rdtsc
    ctl->intercept_misc2    |= 0x7Fu;                                 // vmrun..skinit
    ctl->intercept_misc2    |= (1u << 7);                             // rdtscp

    ctl->guest_asid = 1;
    ctl->np_enable  = SVM_NP_ENABLE;
    ctl->ncr3       = MmGetPhysicalAddress(&tables->pml4[0]).QuadPart;
    ctl->msrpm_base_pa = MmGetPhysicalAddress(tables->msrpm).QuadPart;

    st->gdtr.base   = ctx->gdtr_base;
    st->gdtr.limit  = ctx->gdtr_limit;
    st->idtr.base   = ctx->idtr_base;
    st->idtr.limit  = ctx->idtr_limit;

    st->cs.selector = ctx->cs;
    st->ds.selector = ctx->ds;
    st->es.selector = ctx->es;
    st->ss.selector = ctx->ss;

    st->cs.limit = (ULONG)__segmentlimit(ctx->cs);
    st->ds.limit = (ULONG)__segmentlimit(ctx->ds);
    st->es.limit = (ULONG)__segmentlimit(ctx->es);
    st->ss.limit = (ULONG)__segmentlimit(ctx->ss);

    st->cs.attrib = h7_seg_attrib(ctx->gdtr_base, ctx->cs);
    st->ds.attrib = h7_seg_attrib(ctx->gdtr_base, ctx->ds);
    st->es.attrib = h7_seg_attrib(ctx->gdtr_base, ctx->es);
    st->ss.attrib = h7_seg_attrib(ctx->gdtr_base, ctx->ss);

    st->efer   = ctx->efer;
    st->cr0    = ctx->cr0;
    st->cr2    = ctx->cr2;
    st->cr3    = ctx->cr3;
    st->cr4    = ctx->cr4;
    st->rflags = ctx->rflags;
    st->rsp    = ctx->rsp;
    st->rip    = ctx->rip;
    st->gpat   = ctx->gpat;

    ULONG64 gpa = MmGetPhysicalAddress(&cpu->guest_vmcb).QuadPart;
    ULONG64 hpa = MmGetPhysicalAddress(&cpu->host_vmcb).QuadPart;
    __svm_vmsave(gpa);

    cpu->top.guest_vmcb_pa = gpa;
    cpu->top.host_vmcb_pa  = hpa;
    cpu->top.self          = cpu;
    cpu->top.npt           = tables;
    cpu->top.sentinel      = H7_SENTINEL;

    ULONG64 hsave_pa = MmGetPhysicalAddress(cpu->host_save_area).QuadPart;
    __writemsr(MSR_VM_HSAVE_PA, hsave_pa);
    __svm_vmsave(hpa);
}
