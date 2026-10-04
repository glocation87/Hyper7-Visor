#pragma once
#include <ntddk.h>

/* MSRs */
#define MSR_EFER            0xC0000080
#define MSR_STAR            0xC0000081
#define MSR_LSTAR           0xC0000082
#define MSR_CSTAR           0xC0000083
#define MSR_FMASK           0xC0000084
#define MSR_PAT             0x00000277
#define MSR_VM_CR           0xC0010114
#define MSR_VM_HSAVE_PA     0xC0010117

#define EFER_SVME           (1ULL << 12)
#define VM_CR_SVMDIS        (1ULL << 4)
#define VM_CR_LOCK          (1ULL << 3)

/* CPUID leaves we care about */
#define CPUID_VENDOR                0x00000000
#define CPUID_FEATURES              0x00000001
#define CPUID_EXT_FEATURES          0x80000001
#define CPUID_SVM_FEATURES          0x8000000A
#define CPUID_HV_VENDOR             0x40000000
#define CPUID_HV_INTERFACE          0x40000001

/* our private leaves */
#define CPUID_HV_PING               0x40000010   /* ecx comes back 'Hyp7' if live */
#define CPUID_HV_UNLOAD             0x40000011   /* devirtualize this cpu */

#define CPUID_PING_MAGIC            'Hyp7'

/* feature bits */
#define FEAT_ECX_HYPERVISOR         (1 << 31)
#define EXT_ECX_SVM                 (1 << 2)
#define EXT_EDX_LONG_MODE           (1 << 29)
#define SVM_EDX_NP                  (1 << 0)

// vmexit codes - AMD Vol.2 Appendix C
#define VMEXIT_CR3_WRITE    0x13
#define VMEXIT_CPUID        0x72
#define VMEXIT_RDTSC        0x6E
#define VMEXIT_RDTSCP       0x87
#define VMEXIT_MSR          0x7C
#define VMEXIT_VMRUN        0x80
#define VMEXIT_VMMCALL      0x81
#define VMEXIT_VMLOAD       0x82
#define VMEXIT_VMSAVE       0x83
#define VMEXIT_STGI         0x84
#define VMEXIT_CLGI         0x85
#define VMEXIT_SKINIT       0x86
#define VMEXIT_XSETBV       0x8D
#define VMEXIT_SHUTDOWN     0x7F
#define VMEXIT_NPF          0x400
#define VMEXIT_INVALID      (-1LL)

#define SVM_NP_ENABLE       (1ULL << 0)

/* MSR permission map is 2 pages + 2KB; AMD wants 0x2000 reserved */
#define MSRPM_SIZE          (2 * PAGE_SIZE)

#define PAGE_SHIFT_2MB      21
