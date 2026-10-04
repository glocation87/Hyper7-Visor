# Hyper7

A small AMD-V (SVM) hypervisor written in Rust/C. It virtualizes the
machine it's already running on ("blue pill" style), the same approach as
SimpleSvm and jonomango's hv.

Two pieces:

- `driver/` — WDM kernel driver, C + x64 asm. Does the actual SVM work:
  feature detection, per-CPU VMCB setup, nested paging, the `vmrun` loop and
  the `#VMEXIT` handler.
- `ctl/` — Rust user-mode tool. Installs/starts/stops the driver service and
  talks to it over IOCTLs for everything else.

Not stealthy on purpose. CPUID leaf `0x40000000` reports the vendor
`Hyper7Visor ` so you can tell it's live, i might change soon.

## Building

Driver (needs VS2022 + Windows Kit, run from repo root):

    build.bat

With tests:

    build.bat test

Controller:

    cd ctl && cargo build --release

## Running

Driver has to be signed, so either boot with test signing on
(`bcdedit /set testsigning on`, reboot) and sign the `.sys`, or load it under
a kernel debugger. SVM also has to be enabled in BIOS, and Hyper-V/VBS off
(it grabs SVM first otherwise).

    hv-ctl install     # create + start the service
    hv-ctl remove      # stop + uninstall
    hv-ctl status

## What it does

### Core virtualization
- SVM support probe (AMD vendor, SVM feature bit, NPT, VM_CR unlocked)
- Per-CPU VMCB with host/guest state, allocated as `h7_vcpu`
- Identity-mapped 2MB nested page tables (all 512GB)
- MSR permission bitmap intercepting EFER and LSTAR
- DPC broadcast to run virtualize / devirt on every active CPU

### VMEXIT handlers
- `#CPUID` — exposes the `Hyper7Visor` vendor string and private leaves
- `#MSR`  — intercepts EFER (keeps SVME set), LSTAR (traced, shimmable)
- `#RDTSC` / `#RDTSCP` — pass-through plus a per-vcpu TSC offset
- `#CR3 WRITE` — ring buffer of recent CR3 values, togglable
- `#VMMCALL` — ring-0 hypercall ABI (num in rax, args in rcx/rdx/r8)
- `#NPF` — split-page hook flipper
- `#SHUTDOWN` — clean devirt instead of bugcheck
- SVM instructions from the guest (`VMRUN` etc.) → inject `#UD`

### User-mode interface (`\\.\hv7`)
`hv-ctl` commands:

    hv-ctl ping                       # cpuid probe + vmmcall round-trip
    hv-ctl stats                      # per-exit counters
    hv-ctl cr3 on | off | sample      # watch guest address-space switches
    hv-ctl lstar                      # read current syscall-entry MSR
    hv-ctl read <gva-hex> <len> [cr3-hex]
                                      # walk guest PTs, dump guest memory
    hv-ctl hook list
    hv-ctl hook add <gpa-hex> <patched.bin>
                                      # install an invisible 4KB code hook

### Memory
- Guest VA → GPA walker (handles 4KB / 2MB / 1GB pages)
- Guest-memory read/write by VA, using either the current CR3 or a
  specified one.

### Code hooks (NPT split-page)
- `hook add` takes a 4KB-aligned guest physical address and a replacement
  page. The hypervisor splits the containing 2MB NPT entry into 4KB PTEs,
  then flips the PTE between an exec-only shadow (hooked bytes) and a
  R/W-only original (clean bytes) in response to `#NPF`s. Memory inspection
  from inside the guest sees the original page; the CPU executes the hooked
  one.

## References

- AMD64 Architecture Programmer's Manual Vol. 2 (System Programming), 24593
- tandasat/SimpleSvm
- jonomango/hv

# Credits
https://github.com/IceCoaled/Amd-Hypervisor-Base ~ IceCoaled, HV Base
