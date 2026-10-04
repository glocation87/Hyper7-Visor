use std::env;
use std::ffi::CString;
use std::path::PathBuf;
use std::ptr;

use windows_sys::Win32::Foundation::*;
use windows_sys::Win32::System::Services::*;

type ScHandle = *mut core::ffi::c_void;

const SVC_NAME: &str = "hv7";
const DRIVER_FILE: &str = "hv7.sys";

fn get_driver_path() -> PathBuf {
    let exe = env::current_exe().expect("can't find exe path");
    let dir = exe.parent().unwrap();
    let beside = dir.join(DRIVER_FILE);
    if beside.exists() {
        return beside;
    }
    let build = dir.join("..\\..\\build").join(DRIVER_FILE);
    if build.exists() {
        return build.canonicalize().unwrap();
    }
    beside
}

fn open_scm() -> ScHandle {
    unsafe { OpenSCManagerA(ptr::null(), ptr::null(), SC_MANAGER_ALL_ACCESS) }
}

fn install() {
    let scm = open_scm();
    if scm.is_null() {
        eprintln!("failed to open SCM (run as admin)");
        return;
    }

    let path = get_driver_path();
    if !path.exists() {
        eprintln!("driver not found at {}", path.display());
        unsafe { CloseServiceHandle(scm); }
        return;
    }

    let path_str = path.to_str().unwrap();
    let svc_name = CString::new(SVC_NAME).unwrap();
    let bin_path = CString::new(path_str).unwrap();

    let svc = unsafe {
        CreateServiceA(
            scm,
            svc_name.as_ptr() as *const u8,
            svc_name.as_ptr() as *const u8,
            SERVICE_ALL_ACCESS,
            SERVICE_KERNEL_DRIVER,
            SERVICE_DEMAND_START,
            SERVICE_ERROR_NORMAL,
            bin_path.as_ptr() as *const u8,
            ptr::null(), ptr::null_mut(), ptr::null(), ptr::null(), ptr::null(),
        )
    };

    if svc.is_null() {
        let err = unsafe { GetLastError() };
        if err == ERROR_SERVICE_EXISTS {
            println!("service already exists, starting...");
            start();
        } else {
            eprintln!("CreateService failed: {err}");
        }
        unsafe { CloseServiceHandle(scm); }
        return;
    }

    println!("service created, starting...");
    unsafe {
        if StartServiceA(svc, 0, ptr::null()) == 0 {
            let err = GetLastError();
            if err != ERROR_SERVICE_ALREADY_RUNNING {
                eprintln!("StartService failed: {err}");
            }
        } else {
            println!("hypervisor loaded");
        }
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
    }
}

fn start() {
    let scm = open_scm();
    if scm.is_null() { eprintln!("SCM failed"); return; }

    let name = CString::new(SVC_NAME).unwrap();
    let svc = unsafe {
        OpenServiceA(scm, name.as_ptr() as *const u8, SERVICE_START)
    };
    if svc.is_null() {
        eprintln!("can't open service (not installed?)");
        unsafe { CloseServiceHandle(scm); }
        return;
    }

    unsafe {
        if StartServiceA(svc, 0, ptr::null()) == 0 {
            let err = GetLastError();
            if err == ERROR_SERVICE_ALREADY_RUNNING {
                println!("already running");
            } else {
                eprintln!("start failed: {err}");
            }
        } else {
            println!("started");
        }
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
    }
}

fn stop_svc() {
    let scm = open_scm();
    if scm.is_null() { eprintln!("SCM failed"); return; }

    let name = CString::new(SVC_NAME).unwrap();
    let svc = unsafe {
        OpenServiceA(scm, name.as_ptr() as *const u8, SERVICE_STOP)
    };
    if svc.is_null() {
        eprintln!("can't open service");
        unsafe { CloseServiceHandle(scm); }
        return;
    }

    let mut status = SERVICE_STATUS {
        dwServiceType: 0,
        dwCurrentState: 0,
        dwControlsAccepted: 0,
        dwWin32ExitCode: 0,
        dwServiceSpecificExitCode: 0,
        dwCheckPoint: 0,
        dwWaitHint: 0,
    };

    unsafe {
        if ControlService(svc, SERVICE_CONTROL_STOP, &mut status) == 0 {
            eprintln!("stop failed: {}", GetLastError());
        } else {
            println!("stopped");
        }
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
    }
}

fn remove() {
    stop_svc();

    let scm = open_scm();
    if scm.is_null() { return; }

    let name = CString::new(SVC_NAME).unwrap();
    // DELETE = 0x00010000
    let svc = unsafe {
        OpenServiceA(scm, name.as_ptr() as *const u8, 0x00010000)
    };
    if svc.is_null() {
        unsafe { CloseServiceHandle(scm); }
        return;
    }

    unsafe {
        if DeleteService(svc) == 0 {
            eprintln!("delete failed: {}", GetLastError());
        } else {
            println!("service removed");
        }
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
    }
}

fn status() {
    let scm = open_scm();
    if scm.is_null() { eprintln!("SCM failed"); return; }

    let name = CString::new(SVC_NAME).unwrap();
    let svc = unsafe {
        OpenServiceA(scm, name.as_ptr() as *const u8, SERVICE_QUERY_STATUS)
    };
    if svc.is_null() {
        println!("service not installed");
        unsafe { CloseServiceHandle(scm); }
        return;
    }

    let mut st = SERVICE_STATUS {
        dwServiceType: 0, dwCurrentState: 0, dwControlsAccepted: 0,
        dwWin32ExitCode: 0, dwServiceSpecificExitCode: 0,
        dwCheckPoint: 0, dwWaitHint: 0,
    };

    unsafe {
        if QueryServiceStatus(svc, &mut st) != 0 {
            let state = match st.dwCurrentState {
                SERVICE_STOPPED => "stopped",
                SERVICE_RUNNING => "running",
                SERVICE_START_PENDING => "starting",
                SERVICE_STOP_PENDING => "stopping",
                _ => "unknown",
            };
            println!("hv7: {state}");
        }
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
    }
}

fn ping() {
    #[cfg(target_arch = "x86_64")]
    {
        let ecx: u32;
        unsafe {
            // rbx is reserved by LLVM, so we save/restore it manually
            std::arch::asm!(
                "push rbx",
                "cpuid",
                "pop rbx",
                in("eax") 0x40000010u32,
                in("ecx") 0u32,
                lateout("ecx") ecx,
                lateout("edx") _,
                lateout("eax") _,
            );
        }
        let magic = u32::from_le_bytes(*b"Hyp7");
        if ecx == magic {
            println!("hypervisor is live!");
        } else {
            println!("hypervisor not detected (ecx={ecx:#x})");
        }
    }

    #[cfg(not(target_arch = "x86_64"))]
    {
        eprintln!("ping only works on x86_64");
    }
}

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        println!("usage: hv-ctl <install|start|stop|remove|status|ping>");
        return;
    }

    match args[1].as_str() {
        "install" => install(),
        "start"   => start(),
        "stop"    => stop_svc(),
        "remove"  => remove(),
        "status"  => status(),
        "ping"    => ping(),
        other     => eprintln!("unknown command: {other}"),
    }
}
