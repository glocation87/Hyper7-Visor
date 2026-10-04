# Hyper7 

A small AMD-V (SVM) hypervisor written in Rust/C. It virtualizes the
machine it's already running on ("blue pill" style), the same approach as
SimpleSvm and jonomango's hv.

Two pieces:

- `driver/` — WDM kernel driver, C + x64 asm. Does the actual SVM work:
  feature detection, per-CPU VMCB setup, nested paging, the `vmrun` loop and
  the `#VMEXIT` handler.
- `ctl/` — Rust user-mode tool. Installs/starts/stops the driver service and
  talks to it over an IOCTL (status, a vmmcall-based "are we virtualized?"
  probe, unload).

Not stealthy on purpose. CPUID leaf `0x40000000` reports the vendor
`Hyper7Visor ` so you can tell it's live, i might change soon

## Building

Driver (needs VS2022 + Windows Kit, run from the repo root):

    build.bat

Controller:

    cd ctl && cargo build --release

## Running

driver has to be signed, so either boot with test signing on
(`bcdedit /set testsigning on`, reboot) and sign the `.sys`, or load it under
a kernel debugger. SVM also has to be enabled in BIOS, and Hyper-V/VBS off (it
grabs SVM first otherwise).

    hv-ctl install    # create + start the service
    hv-ctl status
    hv-ctl ping        # vmmcall round-trip
    hv-ctl remove

## References

- AMD64 Architecture Programmer's Manual Vol. 2 (System Programming), 24593
- tandasat/SimpleSvm
- jonomango/hv
