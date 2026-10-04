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

fn cmd_lstar() {
    let d = match device::Device::open() { Ok(d) => d, Err(e) => { eprintln!("open: {e}"); return; } };
    match d.lstar() {
        Ok(v) => println!("current LSTAR (syscall entry): {v:#x}"),
        Err(e) => eprintln!("lstar: {e}"),
    }
}

fn parse_hex(s: &str) -> Option<u64> {
    let s = s.strip_prefix("0x").unwrap_or(s);
    u64::from_str_radix(s, 16).ok()
}

fn cmd_read(args: &[String]) {
    if args.len() < 4 {
        eprintln!("usage: hv-ctl read <gva-hex> <len> [cr3-hex]");
        return;
    }
    let gva = match parse_hex(&args[2]) { Some(v) => v, None => { eprintln!("bad gva"); return; } };
    let len: u32 = match args[3].parse() { Ok(v) => v, Err(_) => { eprintln!("bad len"); return; } };
    let cr3 = args.get(4).and_then(|s| parse_hex(s)).unwrap_or(0);

    let d = match device::Device::open() { Ok(d) => d, Err(e) => { eprintln!("open: {e}"); return; } };
    match d.read_gva(cr3, gva, len) {
        Ok(bytes) => {
            for (i, chunk) in bytes.chunks(16).enumerate() {
                print!("{:08x}  ", i * 16);
                for b in chunk { print!("{b:02x} "); }
                for _ in chunk.len()..16 { print!("   "); }
                print!(" |");
                for b in chunk {
                    let c = if (0x20..=0x7e).contains(b) { *b as char } else { '.' };
                    print!("{c}");
                }
                println!("|");
            }
        }
        Err(e) => eprintln!("read: {e}"),
    }
}

fn cmd_hook(args: &[String]) {
    let d = match device::Device::open() { Ok(d) => d, Err(e) => { eprintln!("open: {e}"); return; } };
    let sub = args.get(2).map(String::as_str).unwrap_or("list");
    match sub {
        "list" => match d.hook_list() {
            Ok(list) if list.is_empty() => println!("no hooks installed"),
            Ok(list) => {
                for (i, h) in list.iter().enumerate() {
                    println!("[{i}] gpa={:#x} shadow={:#x} active={}", h.target_gpa, h.shadow_pa, h.active);
                }
            }
            Err(e) => eprintln!("list: {e}"),
        },
        "add" => {
            if args.len() < 5 {
                eprintln!("usage: hv-ctl hook add <target-gpa-hex> <patched-file.bin>");
                return;
            }
            let gpa = match parse_hex(&args[3]) { Some(v) => v, None => { eprintln!("bad gpa"); return; } };
            let data = match std::fs::read(&args[4]) {
                Ok(d) => d,
                Err(e) => { eprintln!("file read: {e}"); return; }
            };
            if data.len() != 4096 {
                eprintln!("patched file must be exactly 4096 bytes (got {})", data.len());
                return;
            }
            match d.hook_add(gpa, &data) {
                Ok(_)  => println!("hook installed at {gpa:#x}"),
                Err(e) => eprintln!("hook: 0x{e:x}"),
            }
        }
        _ => eprintln!("usage: hv-ctl hook <list|add>"),
    }
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
        println!("usage: hv-ctl <install|start|stop|remove|status|ping|stats|cr3|lstar|read|hook>");
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
        "lstar"   => cmd_lstar(),
        "read"    => cmd_read(&args),
        "hook"    => cmd_hook(&args),
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
        assert_eq!(std::mem::size_of::<device::Stats>(), 72);
    }

    #[test]
    fn hook_info_matches_c() {
        // c struct: u64 target + u64 shadow + u32 active + 4 pad = 24
        assert_eq!(std::mem::size_of::<device::HookInfo>(), 24);
    }

    #[test]
    fn hook_req_is_4kb_plus_header() {
        assert_eq!(std::mem::size_of::<device::HookReq>(), 8 + 4096);
    }
}
