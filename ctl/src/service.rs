use std::env;
use std::ffi::CString;
use std::path::PathBuf;
use std::ptr;

use windows_sys::Win32::Foundation::*;
use windows_sys::Win32::System::Services::*;

type ScHandle = *mut core::ffi::c_void;

pub const SVC_NAME: &str = "hv7";
const DRIVER_FILE: &str = "hv7.sys";

fn driver_path() -> PathBuf {
    let exe = env::current_exe().expect("exe path");
    let dir = exe.parent().unwrap();
    let beside = dir.join(DRIVER_FILE);
    if beside.exists() { return beside; }
    let build = dir.join("..\\..\\build").join(DRIVER_FILE);
    if build.exists() { return build.canonicalize().unwrap(); }
    beside
}

fn open_scm() -> ScHandle {
    unsafe { OpenSCManagerA(ptr::null(), ptr::null(), SC_MANAGER_ALL_ACCESS) }
}

pub fn install() {
    let scm = open_scm();
    if scm.is_null() { eprintln!("open scm failed (admin?)"); return; }

    let path = driver_path();
    if !path.exists() { eprintln!("driver not at {}", path.display());
        unsafe { CloseServiceHandle(scm); } return; }

    let svc_name = CString::new(SVC_NAME).unwrap();
    let bin_path = CString::new(path.to_str().unwrap()).unwrap();

    let svc = unsafe {
        CreateServiceA(scm,
            svc_name.as_ptr() as *const u8,
            svc_name.as_ptr() as *const u8,
            SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER,
            SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
            bin_path.as_ptr() as *const u8,
            ptr::null(), ptr::null_mut(), ptr::null(), ptr::null(), ptr::null())
    };

    if svc.is_null() {
        let err = unsafe { GetLastError() };
        if err == ERROR_SERVICE_EXISTS {
            start();
        } else {
            eprintln!("CreateService: {err}");
        }
        unsafe { CloseServiceHandle(scm); }
        return;
    }

    unsafe {
        if StartServiceA(svc, 0, ptr::null()) == 0 {
            let err = GetLastError();
            if err != ERROR_SERVICE_ALREADY_RUNNING {
                eprintln!("StartService: {err}");
            }
        } else {
            println!("loaded");
        }
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
    }
}

pub fn start() {
    let scm = open_scm();
    if scm.is_null() { eprintln!("scm"); return; }
    let name = CString::new(SVC_NAME).unwrap();
    let svc = unsafe { OpenServiceA(scm, name.as_ptr() as *const u8, SERVICE_START) };
    if svc.is_null() { eprintln!("not installed?"); unsafe { CloseServiceHandle(scm); } return; }
    unsafe {
        if StartServiceA(svc, 0, ptr::null()) == 0 {
            let err = GetLastError();
            if err == ERROR_SERVICE_ALREADY_RUNNING { println!("already running"); }
            else { eprintln!("start: {err}"); }
        } else { println!("started"); }
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
    }
}

pub fn stop() {
    let scm = open_scm();
    if scm.is_null() { eprintln!("scm"); return; }
    let name = CString::new(SVC_NAME).unwrap();
    let svc = unsafe { OpenServiceA(scm, name.as_ptr() as *const u8, SERVICE_STOP) };
    if svc.is_null() { eprintln!("open"); unsafe { CloseServiceHandle(scm); } return; }
    let mut st: SERVICE_STATUS = unsafe { std::mem::zeroed() };
    unsafe {
        if ControlService(svc, SERVICE_CONTROL_STOP, &mut st) == 0 {
            eprintln!("stop: {}", GetLastError());
        } else { println!("stopped"); }
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
    }
}

pub fn remove() {
    stop();
    let scm = open_scm();
    if scm.is_null() { return; }
    let name = CString::new(SVC_NAME).unwrap();
    const DELETE_ACC: u32 = 0x10000;
    let svc = unsafe { OpenServiceA(scm, name.as_ptr() as *const u8, DELETE_ACC) };
    if svc.is_null() { unsafe { CloseServiceHandle(scm); } return; }
    unsafe {
        if DeleteService(svc) == 0 { eprintln!("delete: {}", GetLastError()); }
        else { println!("removed"); }
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
    }
}

pub fn status() {
    let scm = open_scm();
    if scm.is_null() { eprintln!("scm"); return; }
    let name = CString::new(SVC_NAME).unwrap();
    let svc = unsafe { OpenServiceA(scm, name.as_ptr() as *const u8, SERVICE_QUERY_STATUS) };
    if svc.is_null() { println!("not installed"); unsafe { CloseServiceHandle(scm); } return; }
    let mut st: SERVICE_STATUS = unsafe { std::mem::zeroed() };
    unsafe {
        if QueryServiceStatus(svc, &mut st) != 0 {
            let s = match st.dwCurrentState {
                SERVICE_STOPPED => "stopped",
                SERVICE_RUNNING => "running",
                SERVICE_START_PENDING => "starting",
                SERVICE_STOP_PENDING => "stopping",
                _ => "unknown",
            };
            println!("hv7: {s}");
        }
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
    }
}
