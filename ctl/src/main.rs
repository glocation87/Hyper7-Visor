mod service;
mod device;

use std::env;

fn ping_cpuid() -> u32 {
    #[cfg(target_arch = "x86_64")]
    unsafe {
        let ecx: u32;
        std::arch::asm!(
            "push rbx", "cpuid", "pop rbx",
            in("eax") 0x40000010u32,
            in("ecx") 0u32,
            lateout("ecx") ecx,
            lateout("edx") _,
            lateout("eax") _,
        );
        return ecx;
    }
    #[cfg(not(target_arch = "x86_64"))]
    0
}

fn cmd_ping() {
    let magic = u32::from_le_bytes(*b"Hyp7");
    let ecx = ping_cpuid();
    if ecx == magic {
        println!("cpuid: hypervisor live");
    } else {
        println!("cpuid: no hypervisor (ecx={ecx:#x})");
        return;
    }

    match device::Device::open() {
        Ok(d) => match d.ping() {
            Ok(v) => println!("ioctl: hypercall returned {v:#x}"),
            Err(e) => eprintln!("ioctl ping failed: {e}"),
        },
        Err(e) => eprintln!("open device: {e} (service running?)"),
    }
}

fn cmd_stats() {
    let d = match device::Device::open() {
        Ok(d) => d,
        Err(e) => { eprintln!("open: {e}"); return; }
    };
    let s = match d.stats() {
        Ok(s) => s,
        Err(e) => { eprintln!("stats: {e}"); return; }
    };
    println!("vmexit stats:");
    println!("  total        {}", s.total);
    println!("  cpuid        {}", s.cpuid);
    println!("  msr          {}", s.msr);
    println!("  rdtsc        {}", s.rdtsc);
    println!("  rdtscp       {}", s.rdtscp);
    println!("  vmmcall      {}", s.vmmcall);
    println!("  cr3 write    {}", s.cr3_write);
    println!("  npf          {}", s.npf);
    println!("  injected #UD {}", s.injected_ud);
}

fn cmd_cr3(args: &[String]) {
    let d = match device::Device::open() {
        Ok(d) => d,
        Err(e) => { eprintln!("open: {e}"); return; }
    };
    let sub = args.get(2).map(String::as_str).unwrap_or("sample");
    match sub {
        "on"  => { d.cr3_watch(true).ok(); println!("cr3 watch enabled"); }
        "off" => { d.cr3_watch(false).ok(); println!("cr3 watch disabled"); }
        "sample" => match d.cr3_sample() {
            Ok(0)   => println!("no cr3 samples yet (enable with: hv-ctl cr3 on)"),
            Ok(cr3) => println!("last cr3: {cr3:#x}"),
            Err(e)  => eprintln!("sample: {e}"),
        },
        _ => eprintln!("usage: hv-ctl cr3 <on|off|sample>"),
    }
}

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        println!("usage: hv-ctl <install|start|stop|remove|status|ping|stats|cr3>");
        return;
    }
    match args[1].as_str() {
        "install" => service::install(),
        "start"   => service::start(),
        "stop"    => service::stop(),
        "remove"  => service::remove(),
        "status"  => service::status(),
        "ping"    => cmd_ping(),
        "stats"   => cmd_stats(),
        "cr3"     => cmd_cr3(&args),
        other     => eprintln!("unknown: {other}"),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ping_magic_encoding() {
        assert_eq!(u32::from_le_bytes(*b"Hyp7"), 0x37707948);
    }

    #[test]
    fn ioctl_codes_match_driver() {
        // mirrors h7_abi.h: CTL_CODE(FILE_DEVICE_UNKNOWN=0x22, fn, METHOD_BUFFERED=0, 0)
        assert_eq!(device::IOCTL_PING,   (0x22 << 16) | (0x800 << 2));
        assert_eq!(device::IOCTL_STATS,  (0x22 << 16) | (0x801 << 2));
        assert_eq!(device::IOCTL_UNLOAD, (0x22 << 16) | (0x802 << 2));
    }

    #[test]
    fn stats_struct_matches_c_layout() {
        // 9 u64s, matches h7_stats_out
        assert_eq!(std::mem::size_of::<device::Stats>(), 72);
    }
}
