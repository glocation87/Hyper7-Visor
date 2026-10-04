use std::ptr;
use windows_sys::Win32::Foundation::*;
use windows_sys::Win32::Storage::FileSystem::*;
use windows_sys::Win32::System::IO::*;

const DOS_PATH: &str = r"\\.\hv7";

const FILE_DEVICE_UNKNOWN: u32 = 0x22;
const METHOD_BUFFERED: u32 = 0;

const fn ctl_code(dev: u32, fun: u32, m: u32, acc: u32) -> u32 {
    (dev << 16) | (acc << 14) | (fun << 2) | m
}

pub const IOCTL_PING:       u32 = ctl_code(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, 0);
pub const IOCTL_STATS:      u32 = ctl_code(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, 0);
#[allow(dead_code)]
pub const IOCTL_UNLOAD:     u32 = ctl_code(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, 0);
pub const IOCTL_CR3_WATCH:  u32 = ctl_code(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, 0);
pub const IOCTL_CR3_SAMPLE: u32 = ctl_code(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, 0);
pub const IOCTL_LSTAR:      u32 = ctl_code(FILE_DEVICE_UNKNOWN, 0x805, METHOD_BUFFERED, 0);
pub const IOCTL_READ_GVA:   u32 = ctl_code(FILE_DEVICE_UNKNOWN, 0x806, METHOD_BUFFERED, 0);
pub const IOCTL_HOOK_ADD:   u32 = ctl_code(FILE_DEVICE_UNKNOWN, 0x807, METHOD_BUFFERED, 0);
pub const IOCTL_HOOK_LIST:  u32 = ctl_code(FILE_DEVICE_UNKNOWN, 0x808, METHOD_BUFFERED, 0);

#[repr(C)]
pub struct ReadGvaReq {
    pub cr3: u64,
    pub gva: u64,
    pub len: u32,
}

#[repr(C)]
pub struct HookReq {
    pub target_gpa: u64,
    pub patched: [u8; 4096],
}

#[repr(C)]
#[derive(Default, Debug, Copy, Clone)]
pub struct HookInfo {
    pub target_gpa: u64,
    pub shadow_pa: u64,
    pub active: u32,
    _pad: u32,
}

#[repr(C)]
#[derive(Default, Debug)]
pub struct Stats {
    pub total: u64,
    pub cpuid: u64,
    pub msr: u64,
    pub rdtsc: u64,
    pub rdtscp: u64,
    pub vmmcall: u64,
    pub cr3_write: u64,
    pub npf: u64,
    pub injected_ud: u64,
}

pub struct Device(HANDLE);

impl Device {
    pub fn open() -> Result<Self, u32> {
        let name = std::ffi::CString::new(DOS_PATH).unwrap();
        let h = unsafe {
            CreateFileA(
                name.as_ptr() as *const u8,
                GENERIC_READ | GENERIC_WRITE,
                0, ptr::null(),
                OPEN_EXISTING, 0,
                ptr::null_mut(),
            )
        };
        if h == INVALID_HANDLE_VALUE {
            Err(unsafe { GetLastError() })
        } else {
            Ok(Device(h))
        }
    }

    fn ioctl<T: Default>(&self, code: u32) -> Result<T, u32> {
        let mut out = T::default();
        let mut returned = 0u32;
        let ok = unsafe {
            DeviceIoControl(
                self.0, code,
                ptr::null(), 0,
                &mut out as *mut _ as *mut _, std::mem::size_of::<T>() as u32,
                &mut returned, ptr::null_mut(),
            )
        };
        if ok == 0 { Err(unsafe { GetLastError() }) } else { Ok(out) }
    }

    #[allow(dead_code)]
    fn ioctl_void(&self, code: u32) -> Result<(), u32> {
        let mut returned = 0u32;
        let ok = unsafe {
            DeviceIoControl(
                self.0, code,
                ptr::null(), 0,
                ptr::null_mut(), 0,
                &mut returned, ptr::null_mut(),
            )
        };
        if ok == 0 { Err(unsafe { GetLastError() }) } else { Ok(()) }
    }

    pub fn ping(&self)  -> Result<u64, u32>   { self.ioctl::<u64>(IOCTL_PING) }
    pub fn stats(&self) -> Result<Stats, u32> { self.ioctl::<Stats>(IOCTL_STATS) }
    #[allow(dead_code)]
    pub fn unload(&self) -> Result<(), u32>   { self.ioctl_void(IOCTL_UNLOAD) }

    pub fn cr3_watch(&self, on: bool) -> Result<(), u32> {
        let v: u32 = if on { 1 } else { 0 };
        let mut returned = 0u32;
        let ok = unsafe {
            DeviceIoControl(
                self.0, IOCTL_CR3_WATCH,
                &v as *const _ as *mut _, 4,
                ptr::null_mut(), 0,
                &mut returned, ptr::null_mut(),
            )
        };
        if ok == 0 { Err(unsafe { GetLastError() }) } else { Ok(()) }
    }

    pub fn cr3_sample(&self) -> Result<u64, u32> { self.ioctl::<u64>(IOCTL_CR3_SAMPLE) }

    pub fn lstar(&self) -> Result<u64, u32> { self.ioctl::<u64>(IOCTL_LSTAR) }

    pub fn read_gva(&self, cr3: u64, gva: u64, len: u32) -> Result<Vec<u8>, u32> {
        let req = ReadGvaReq { cr3, gva, len };
        let mut buf = vec![0u8; len as usize];
        let mut returned = 0u32;
        let ok = unsafe {
            DeviceIoControl(
                self.0, IOCTL_READ_GVA,
                &req as *const _ as *mut _, std::mem::size_of::<ReadGvaReq>() as u32,
                buf.as_mut_ptr() as *mut _, len,
                &mut returned, ptr::null_mut(),
            )
        };
        if ok == 0 { Err(unsafe { GetLastError() }) } else {
            buf.truncate(returned as usize);
            Ok(buf)
        }
    }

    pub fn hook_add(&self, target_gpa: u64, patched: &[u8]) -> Result<(), u32> {
        if patched.len() != 4096 { return Err(87); }    // ERROR_INVALID_PARAMETER
        let mut req = Box::new(HookReq { target_gpa, patched: [0u8; 4096] });
        req.patched.copy_from_slice(patched);
        let mut returned = 0u32;
        let ok = unsafe {
            DeviceIoControl(
                self.0, IOCTL_HOOK_ADD,
                req.as_ref() as *const _ as *mut _, std::mem::size_of::<HookReq>() as u32,
                ptr::null_mut(), 0,
                &mut returned, ptr::null_mut(),
            )
        };
        if ok == 0 { Err(unsafe { GetLastError() }) } else { Ok(()) }
    }

    pub fn hook_list(&self) -> Result<Vec<HookInfo>, u32> {
        let mut buf = vec![HookInfo::default(); 64];
        let bytes = (buf.len() * std::mem::size_of::<HookInfo>()) as u32;
        let mut returned = 0u32;
        let ok = unsafe {
            DeviceIoControl(
                self.0, IOCTL_HOOK_LIST,
                ptr::null(), 0,
                buf.as_mut_ptr() as *mut _, bytes,
                &mut returned, ptr::null_mut(),
            )
        };
        if ok == 0 { Err(unsafe { GetLastError() }) } else {
            buf.truncate(returned as usize / std::mem::size_of::<HookInfo>());
            Ok(buf)
        }
    }
}

impl Drop for Device {
    fn drop(&mut self) {
        unsafe { CloseHandle(self.0); }
    }
}
