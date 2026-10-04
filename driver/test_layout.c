/*
 * Userspace test: checks VMCB struct offsets against AMD64 APM Vol.2.
 * Compile with: cl /nologo /W3 /TC driver\test_layout.c /Febuild\test_layout.exe
 * (no WDK needed)
 */
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* fake out the kernel types so our headers parse in userspace */
typedef unsigned char  UCHAR;
typedef unsigned short USHORT;
typedef unsigned long  ULONG;
typedef unsigned __int64 ULONG64;
#define PAGE_SIZE 0x1000
#define PAGE_SHIFT 12
#define DECLSPEC_ALIGN(x) __declspec(align(x))
#define static_assert _Static_assert

#include "vmcb.h"
#include "h7_abi.h"

static int fails = 0;

#define CHECK(struc, field, expected_off) do { \
    size_t got = offsetof(struc, field); \
    if (got != (expected_off)) { \
        printf("FAIL: %s.%s offset = 0x%zx, expected 0x%x\n", \
               #struc, #field, got, (unsigned)(expected_off)); \
        fails++; \
    } \
} while(0)

#define CHECK_SIZE(struc, expected) do { \
    size_t got = sizeof(struc); \
    if (got != (expected)) { \
        printf("FAIL: sizeof(%s) = 0x%zx, expected 0x%x\n", \
               #struc, got, (unsigned)(expected)); \
        fails++; \
    } \
} while(0)

void test_vmcb_control(void)
{
    printf("-- vmcb control area --\n");
    CHECK_SIZE(h7_vmcb_ctl, 0x400);

    CHECK(h7_vmcb_ctl, intercept_cr_read,  0x000);
    CHECK(h7_vmcb_ctl, intercept_cr_write, 0x002);
    CHECK(h7_vmcb_ctl, intercept_dr_read,  0x004);
    CHECK(h7_vmcb_ctl, intercept_dr_write, 0x006);
    CHECK(h7_vmcb_ctl, intercept_exceptions, 0x008);
    CHECK(h7_vmcb_ctl, intercept_misc1,    0x00C);
    CHECK(h7_vmcb_ctl, intercept_misc2,    0x010);
    CHECK(h7_vmcb_ctl, intercept_misc3,    0x014);

    CHECK(h7_vmcb_ctl, pause_filter_threshold, 0x03C);
    CHECK(h7_vmcb_ctl, pause_filter_count,     0x03E);
    CHECK(h7_vmcb_ctl, iopm_base_pa,      0x040);
    CHECK(h7_vmcb_ctl, msrpm_base_pa,     0x048);
    CHECK(h7_vmcb_ctl, tsc_offset,        0x050);
    CHECK(h7_vmcb_ctl, guest_asid,        0x058);
    CHECK(h7_vmcb_ctl, tlb_control,       0x05C);
    CHECK(h7_vmcb_ctl, vintr,             0x060);
    CHECK(h7_vmcb_ctl, intr_shadow,       0x068);
    CHECK(h7_vmcb_ctl, exit_code,         0x070);
    CHECK(h7_vmcb_ctl, exit_info1,        0x078);
    CHECK(h7_vmcb_ctl, exit_info2,        0x080);
    CHECK(h7_vmcb_ctl, exit_int_info,     0x088);
    CHECK(h7_vmcb_ctl, np_enable,         0x090);
    CHECK(h7_vmcb_ctl, event_inject,      0x0A8);
    CHECK(h7_vmcb_ctl, ncr3,             0x0B0);
    CHECK(h7_vmcb_ctl, lbr_virt,         0x0B8);
    CHECK(h7_vmcb_ctl, vmcb_clean,       0x0C0);
    CHECK(h7_vmcb_ctl, nrip,             0x0C8);
    CHECK(h7_vmcb_ctl, fetch_count,      0x0D0);
    CHECK(h7_vmcb_ctl, fetch_bytes,      0x0D1);
}

void test_vmcb_save(void)
{
    printf("-- vmcb save area --\n");
    CHECK_SIZE(h7_vmcb_state, 0xC00);

    /* segments: each h7_seg is 16 bytes */
    CHECK(h7_vmcb_state, es,    0x000);
    CHECK(h7_vmcb_state, cs,    0x010);
    CHECK(h7_vmcb_state, ss,    0x020);
    CHECK(h7_vmcb_state, ds,    0x030);
    CHECK(h7_vmcb_state, fs,    0x040);
    CHECK(h7_vmcb_state, gs,    0x050);
    CHECK(h7_vmcb_state, gdtr,  0x060);
    CHECK(h7_vmcb_state, ldtr,  0x070);
    CHECK(h7_vmcb_state, idtr,  0x080);
    CHECK(h7_vmcb_state, tr,    0x090);

    CHECK(h7_vmcb_state, cpl,   0x0CB);
    CHECK(h7_vmcb_state, efer,  0x0D0);
    CHECK(h7_vmcb_state, cr4,   0x148);
    CHECK(h7_vmcb_state, cr3,   0x150);
    CHECK(h7_vmcb_state, cr0,   0x158);
    CHECK(h7_vmcb_state, dr7,   0x160);
    CHECK(h7_vmcb_state, dr6,   0x168);
    CHECK(h7_vmcb_state, rflags,0x170);
    CHECK(h7_vmcb_state, rip,   0x178);
    CHECK(h7_vmcb_state, rsp,   0x1D8);
    CHECK(h7_vmcb_state, rax,   0x1F8);
    CHECK(h7_vmcb_state, star,  0x200);
    CHECK(h7_vmcb_state, lstar, 0x208);
    CHECK(h7_vmcb_state, cstar, 0x210);
    CHECK(h7_vmcb_state, sfmask,0x218);
    CHECK(h7_vmcb_state, kernel_gs_base, 0x220);
    CHECK(h7_vmcb_state, sysenter_cs,    0x228);
    CHECK(h7_vmcb_state, sysenter_esp,   0x230);
    CHECK(h7_vmcb_state, sysenter_eip,   0x238);
    CHECK(h7_vmcb_state, cr2,   0x240);
    CHECK(h7_vmcb_state, gpat,  0x268);
}

void test_vmcb_overall(void)
{
    printf("-- vmcb combined --\n");
    CHECK_SIZE(h7_vmcb, PAGE_SIZE);
    CHECK(h7_vmcb, control, 0x000);
    CHECK(h7_vmcb, save,    0x400);
}

void test_seg_size(void)
{
    printf("-- segment descriptor --\n");
    CHECK_SIZE(h7_seg, 16);
    CHECK(h7_seg, selector, 0);
    CHECK(h7_seg, attrib,   2);
    CHECK(h7_seg, limit,    4);
    CHECK(h7_seg, base,     8);
}

void test_stats_abi(void)
{
    printf("-- h7_stats_out --\n");
    CHECK_SIZE(h7_stats_out, 9 * 8);
    CHECK(h7_stats_out, total,       0);
    CHECK(h7_stats_out, cpuid,       8);
    CHECK(h7_stats_out, npf,         56);
    CHECK(h7_stats_out, injected_ud, 64);
}

int main(void)
{
    test_seg_size();
    test_vmcb_control();
    test_vmcb_save();
    test_vmcb_overall();
    test_stats_abi();

    if (fails == 0)
        printf("\nall layout checks passed\n");
    else
        printf("\n%d check(s) FAILED\n", fails);

    return fails;
}
