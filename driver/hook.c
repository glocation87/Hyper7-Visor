#include "hv7.h"

#define MAX_HOOKS       64
#define MAX_SPLIT_PDS   16

// a 4KB PT page used to replace a 2MB large-page PDE
typedef struct DECLSPEC_ALIGN(PAGE_SIZE) _split_pt {
    ULONG64 entries[512];
} split_pt;

typedef struct _hook {
    BOOLEAN  active;
    ULONG64  guest_pa;        // 4KB-aligned
    ULONG64  shadow_pa;       // our patched copy
    ULONG64  *pte;            // the one PTE in our shadow PT
    BOOLEAN  current_is_exec; // true: pte points to shadow with X-only; false: original with R/W only
} hook_t;

typedef struct _split_region {
    ULONG64   pd_index1;      // pdp index
    ULONG64   pd_index2;      // pd index
    split_pt *pt;             // allocated PT replacing the 2MB PDE
} split_region;

static hook_t         g_hooks[MAX_HOOKS];
static split_region   g_splits[MAX_SPLIT_PDS];
static ULONG          g_split_count = 0;
static KSPIN_LOCK     g_hook_lock;
static BOOLEAN        g_init = FALSE;

static void ensure_init(void)
{
    if (!g_init) {
        KeInitializeSpinLock(&g_hook_lock);
        RtlZeroMemory(g_hooks, sizeof(g_hooks));
        RtlZeroMemory(g_splits, sizeof(g_splits));
        g_init = TRUE;
    }
}

// find or create a 4KB-PT split for a given 2MB region
static split_pt *get_or_split(h7_npt *npt, ULONG pdp_i, ULONG pd_i)
{
    for (ULONG i = 0; i < g_split_count; i++) {
        if (g_splits[i].pd_index1 == pdp_i && g_splits[i].pd_index2 == pd_i)
            return g_splits[i].pt;
    }
    if (g_split_count >= MAX_SPLIT_PDS) return NULL;

    split_pt *pt = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(split_pt), 'h7sp');
    if (!pt) return NULL;

    // fill the PT with identity mapping for the 2MB region
    ULONG64 base_pfn = ((ULONG64)pdp_i << 9) | pd_i;      // 2MB-page frame
    base_pfn <<= 9;                                        // -> 4KB-page frame base
    for (ULONG i = 0; i < 512; i++) {
        // present | write | user | pfn
        pt->entries[i] = 1 | (1 << 1) | (1 << 2) | ((base_pfn + i) << 12);
    }

    // swap the 2MB PDE for a 4KB PDE pointing at our PT
    ULONG64 pt_pa = MmGetPhysicalAddress(pt).QuadPart;
    h7_pde_2mb *pde = &npt->pd[pdp_i][pd_i];
    pde->val = 0;
    pde->present = 1;
    pde->write   = 1;
    pde->user    = 1;
    // large_page stays 0 -> this is a PDE pointing to a PT
    pde->pfn     = (pt_pa >> 12) & 0x7FFFFFFF;

    g_splits[g_split_count].pd_index1 = pdp_i;
    g_splits[g_split_count].pd_index2 = pd_i;
    g_splits[g_split_count].pt        = pt;
    g_split_count++;
    return pt;
}

// flip a PTE between "data-only" and "exec-only" view
static void set_pte_exec(ULONG64 *pte, ULONG64 pfn, BOOLEAN exec)
{
    // present+user always; write only when !exec; nx when !exec
    ULONG64 v = 1 | (1 << 2) | (pfn << 12);
    if (exec) {
        // read is okay too (we can't prevent fetch alone without R access on amd NPT)
        // use nx=0 but strip write
    } else {
        v |= (1 << 1);          // write
        v |= (1ULL << 63);      // nx
    }
    *pte = v;
}

NTSTATUS h7_hook_install(ULONG64 target_gpa, const UCHAR *patched_bytes)
{
    ensure_init();
    h7_npt *npt = h7_get_npt();
    if (!npt) return STATUS_DEVICE_NOT_READY;

    if (target_gpa & 0xFFF) return STATUS_INVALID_PARAMETER;

    void *shadow = ExAllocatePool2(POOL_FLAG_NON_PAGED, PAGE_SIZE, 'h7sh');
    if (!shadow) return STATUS_NO_MEMORY;
    RtlCopyMemory(shadow, patched_bytes, PAGE_SIZE);

    ULONG64 shadow_pa = MmGetPhysicalAddress(shadow).QuadPart;

    ULONG pdp_i = (target_gpa >> 30) & 0x1FF;
    ULONG pd_i  = (target_gpa >> 21) & 0x1FF;
    ULONG pt_i  = (target_gpa >> 12) & 0x1FF;

    KIRQL old;
    KeAcquireSpinLock(&g_hook_lock, &old);

    split_pt *pt = get_or_split(npt, pdp_i, pd_i);
    if (!pt) {
        KeReleaseSpinLock(&g_hook_lock, old);
        ExFreePoolWithTag(shadow, 'h7sh');
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    ULONG slot = (ULONG)-1;
    for (ULONG i = 0; i < MAX_HOOKS; i++) {
        if (!g_hooks[i].active) { slot = i; break; }
    }
    if (slot == (ULONG)-1) {
        KeReleaseSpinLock(&g_hook_lock, old);
        ExFreePoolWithTag(shadow, 'h7sh');
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    g_hooks[slot].active          = TRUE;
    g_hooks[slot].guest_pa        = target_gpa;
    g_hooks[slot].shadow_pa       = shadow_pa;
    g_hooks[slot].pte             = &pt->entries[pt_i];
    g_hooks[slot].current_is_exec = TRUE;

    // start in exec mode: PTE points at shadow, nx off
    set_pte_exec(g_hooks[slot].pte, shadow_pa >> 12, TRUE);

    KeReleaseSpinLock(&g_hook_lock, old);
    return STATUS_SUCCESS;
}

NTSTATUS h7_hook_remove(ULONG64 target_gpa)
{
    ensure_init();

    KIRQL old;
    KeAcquireSpinLock(&g_hook_lock, &old);

    for (ULONG i = 0; i < MAX_HOOKS; i++) {
        if (!g_hooks[i].active || g_hooks[i].guest_pa != target_gpa) continue;

        // restore identity mapping
        ULONG64 orig_pfn = target_gpa >> 12;
        *g_hooks[i].pte = 1 | (1 << 1) | (1 << 2) | (orig_pfn << 12);

        void *shadow_va = MmGetVirtualForPhysical((PHYSICAL_ADDRESS){ .QuadPart = g_hooks[i].shadow_pa });
        g_hooks[i].active = FALSE;
        KeReleaseSpinLock(&g_hook_lock, old);
        if (shadow_va) ExFreePoolWithTag(shadow_va, 'h7sh');
        return STATUS_SUCCESS;
    }

    KeReleaseSpinLock(&g_hook_lock, old);
    return STATUS_NOT_FOUND;
}

ULONG h7_hook_list(h7_hook_info *out, ULONG max)
{
    ensure_init();
    ULONG n = 0;
    KIRQL old;
    KeAcquireSpinLock(&g_hook_lock, &old);
    for (ULONG i = 0; i < MAX_HOOKS && n < max; i++) {
        if (!g_hooks[i].active) continue;
        out[n].target_gpa = g_hooks[i].guest_pa;
        out[n].shadow_pa  = g_hooks[i].shadow_pa;
        out[n].active     = 1;
        n++;
    }
    KeReleaseSpinLock(&g_hook_lock, old);
    return n;
}

// called from #NPF handler: if this gpa is hooked, flip view and return TRUE
BOOLEAN h7_hook_handle_npf(ULONG64 gpa, BOOLEAN exec_fault)
{
    ULONG64 page = gpa & ~0xFFFULL;

    for (ULONG i = 0; i < MAX_HOOKS; i++) {
        if (!g_hooks[i].active) continue;
        if (g_hooks[i].guest_pa != page) continue;

        if (exec_fault && !g_hooks[i].current_is_exec) {
            set_pte_exec(g_hooks[i].pte, g_hooks[i].shadow_pa >> 12, TRUE);
            g_hooks[i].current_is_exec = TRUE;
        } else if (!exec_fault && g_hooks[i].current_is_exec) {
            set_pte_exec(g_hooks[i].pte, page >> 12, FALSE);
            g_hooks[i].current_is_exec = FALSE;
        }
        return TRUE;
    }
    return FALSE;
}

void h7_hook_cleanup(void)
{
    if (!g_init) return;
    for (ULONG i = 0; i < MAX_HOOKS; i++) {
        if (!g_hooks[i].active) continue;
        void *shadow_va = MmGetVirtualForPhysical((PHYSICAL_ADDRESS){ .QuadPart = g_hooks[i].shadow_pa });
        if (shadow_va) ExFreePoolWithTag(shadow_va, 'h7sh');
        g_hooks[i].active = FALSE;
    }
    for (ULONG i = 0; i < g_split_count; i++) {
        if (g_splits[i].pt) ExFreePoolWithTag(g_splits[i].pt, 'h7sp');
    }
    g_split_count = 0;
}
