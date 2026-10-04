#include "hv7.h"

// walk guest page tables, cr3 -> physical page containing gva
NTSTATUS h7_gva_to_gpa(ULONG64 cr3, ULONG64 gva, ULONG64 *gpa_out)
{
    PHYSICAL_ADDRESS pa;
    ULONG64 pml4_pa = cr3 & ~0xFFFULL;

    ULONG pml4_i = (gva >> 39) & 0x1FF;
    ULONG pdpt_i = (gva >> 30) & 0x1FF;
    ULONG pd_i   = (gva >> 21) & 0x1FF;
    ULONG pt_i   = (gva >> 12) & 0x1FF;

    pa.QuadPart = pml4_pa;
    ULONG64 *pml4 = (ULONG64 *)MmGetVirtualForPhysical(pa);
    if (!pml4) return STATUS_UNSUCCESSFUL;
    if (!(pml4[pml4_i] & 1)) return STATUS_NOT_FOUND;

    pa.QuadPart = pml4[pml4_i] & 0xFFFFFFFFFF000ULL;
    ULONG64 *pdpt = (ULONG64 *)MmGetVirtualForPhysical(pa);
    if (!pdpt) return STATUS_UNSUCCESSFUL;
    if (!(pdpt[pdpt_i] & 1)) return STATUS_NOT_FOUND;

    // 1GB page
    if (pdpt[pdpt_i] & (1 << 7)) {
        *gpa_out = (pdpt[pdpt_i] & 0xFFFFC0000000ULL) | (gva & 0x3FFFFFFF);
        return STATUS_SUCCESS;
    }

    pa.QuadPart = pdpt[pdpt_i] & 0xFFFFFFFFFF000ULL;
    ULONG64 *pd = (ULONG64 *)MmGetVirtualForPhysical(pa);
    if (!pd) return STATUS_UNSUCCESSFUL;
    if (!(pd[pd_i] & 1)) return STATUS_NOT_FOUND;

    // 2MB page
    if (pd[pd_i] & (1 << 7)) {
        *gpa_out = (pd[pd_i] & 0xFFFFFFE00000ULL) | (gva & 0x1FFFFF);
        return STATUS_SUCCESS;
    }

    pa.QuadPart = pd[pd_i] & 0xFFFFFFFFFF000ULL;
    ULONG64 *pt = (ULONG64 *)MmGetVirtualForPhysical(pa);
    if (!pt) return STATUS_UNSUCCESSFUL;
    if (!(pt[pt_i] & 1)) return STATUS_NOT_FOUND;

    *gpa_out = (pt[pt_i] & 0xFFFFFFFFFF000ULL) | (gva & 0xFFF);
    return STATUS_SUCCESS;
}

NTSTATUS h7_read_gva(ULONG64 cr3, ULONG64 gva, void *out, ULONG len)
{
    if (len > PAGE_SIZE) return STATUS_INVALID_PARAMETER;

    UCHAR *dst = out;
    while (len) {
        ULONG64 gpa;
        NTSTATUS st = h7_gva_to_gpa(cr3, gva, &gpa);
        if (!NT_SUCCESS(st)) return st;

        ULONG page_off = gva & 0xFFF;
        ULONG chunk    = PAGE_SIZE - page_off;
        if (chunk > len) chunk = len;

        PHYSICAL_ADDRESS pa; pa.QuadPart = gpa;
        void *src = MmGetVirtualForPhysical(pa);
        if (!src) return STATUS_UNSUCCESSFUL;

        RtlCopyMemory(dst, src, chunk);
        dst += chunk;
        gva += chunk;
        len -= chunk;
    }
    return STATUS_SUCCESS;
}

NTSTATUS h7_write_gva(ULONG64 cr3, ULONG64 gva, const void *in, ULONG len)
{
    if (len > PAGE_SIZE) return STATUS_INVALID_PARAMETER;

    const UCHAR *src = in;
    while (len) {
        ULONG64 gpa;
        NTSTATUS st = h7_gva_to_gpa(cr3, gva, &gpa);
        if (!NT_SUCCESS(st)) return st;

        ULONG page_off = gva & 0xFFF;
        ULONG chunk    = PAGE_SIZE - page_off;
        if (chunk > len) chunk = len;

        PHYSICAL_ADDRESS pa; pa.QuadPart = gpa;
        void *dst = MmGetVirtualForPhysical(pa);
        if (!dst) return STATUS_UNSUCCESSFUL;

        RtlCopyMemory(dst, src, chunk);
        src += chunk;
        gva += chunk;
        len -= chunk;
    }
    return STATUS_SUCCESS;
}
