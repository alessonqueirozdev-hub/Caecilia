//! Windows: the known folders, the uninstall entry in the registry,
//! shortcuts, deleting at restart, elevation. The only `unsafe` code of
//! the installer, each call a plain Win32 or COM call on values made here.

#![allow(unsafe_code)]

use std::ffi::OsString;
use std::io;
use std::os::windows::ffi::{OsStrExt, OsStringExt};
use std::path::{Path, PathBuf};
use std::ptr;

use windows_sys::Win32::Foundation::{
    CloseHandle, ERROR_CANCELLED, ERROR_SUCCESS, HANDLE, WAIT_OBJECT_0,
};
use windows_sys::Win32::Globalization::GetUserDefaultUILanguage;
use windows_sys::Win32::Security::{
    GetTokenInformation, TOKEN_ELEVATION, TOKEN_QUERY, TokenElevation,
};
use windows_sys::Win32::Storage::FileSystem::{MOVEFILE_DELAY_UNTIL_REBOOT, MoveFileExW};
use windows_sys::Win32::System::Com::CoTaskMemFree;
use windows_sys::Win32::System::Registry::{
    HKEY, HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE, KEY_ALL_ACCESS, KEY_READ, KEY_WOW64_64KEY,
    KEY_WRITE, REG_DWORD, REG_OPTION_NON_VOLATILE, REG_SAM_FLAGS, REG_SZ, RegCloseKey,
    RegCreateKeyExW, RegDeleteTreeW, RegOpenKeyExW, RegQueryValueExW, RegSetValueExW,
};
use windows_sys::Win32::System::Threading::{
    GetCurrentProcess, GetExitCodeProcess, OpenProcessToken, WaitForSingleObject,
};
use windows_sys::Win32::UI::Shell::{
    FOLDERID_CommonPrograms, FOLDERID_Desktop, FOLDERID_ProgramFiles, FOLDERID_ProgramFilesCommon,
    FOLDERID_Programs, FOLDERID_PublicDesktop, FOLDERID_RoamingAppData, FOLDERID_UserProgramFiles,
    KF_FLAG_DONT_VERIFY, SEE_MASK_NOCLOSEPROCESS, SHELLEXECUTEINFOW, SHGetKnownFolderPath,
    ShellExecuteExW,
};
use windows_sys::core::GUID;

use crate::plan::{Places, Scope, Shortcut, UninstallEntry};
use crate::system::{Installed, System};

/// `s` as a NUL-terminated wide string.
fn wide(s: impl AsRef<std::ffi::OsStr>) -> Vec<u16> {
    s.as_ref().encode_wide().chain([0]).collect()
}

/// Where the known folder `id` is, whether or not it exists yet: on a new
/// profile `%LOCALAPPDATA%\Programs` often does not, and the engine makes
/// the folders it needs.
fn known_folder(id: &GUID) -> io::Result<PathBuf> {
    let mut path = ptr::null_mut();
    // SAFETY: `id` is a valid GUID; the system allocates `path`, which is
    // freed below whatever the result.
    let hr =
        unsafe { SHGetKnownFolderPath(id, KF_FLAG_DONT_VERIFY as u32, ptr::null_mut(), &mut path) };
    let result = if hr >= 0 && !path.is_null() {
        // SAFETY: on success `path` is a NUL-terminated wide string.
        let len = unsafe { (0..).take_while(|&i| *path.add(i) != 0).count() };
        // SAFETY: `len` characters were just read from it.
        let chars = unsafe { std::slice::from_raw_parts(path, len) };
        Ok(PathBuf::from(OsString::from_wide(chars)))
    } else {
        Err(io::Error::from_raw_os_error(hr))
    };
    // SAFETY: freeing what SHGetKnownFolderPath allocated (null is fine).
    unsafe { CoTaskMemFree(path.cast()) };
    result
}

/// The folders of this system, for the person running the installer.
pub fn places() -> io::Result<Places> {
    Ok(Places {
        program_files: known_folder(&FOLDERID_ProgramFiles)?,
        common_files: known_folder(&FOLDERID_ProgramFilesCommon)?,
        user_programs: known_folder(&FOLDERID_UserProgramFiles)?,
        app_data: known_folder(&FOLDERID_RoamingAppData)?,
        user_desktop: known_folder(&FOLDERID_Desktop)?,
        common_desktop: known_folder(&FOLDERID_PublicDesktop)?,
        user_start_menu: known_folder(&FOLDERID_Programs)?,
        common_start_menu: known_folder(&FOLDERID_CommonPrograms)?,
        temp: std::env::temp_dir(),
    })
}

/// The language of Windows' interface for this person.
pub fn ui_language() -> u16 {
    // SAFETY: no arguments.
    unsafe { GetUserDefaultUILanguage() }
}

/// Whether this process runs with an administrator's rights.
pub fn is_elevated() -> bool {
    let mut token: HANDLE = ptr::null_mut();
    // SAFETY: the current process's pseudo-handle, and a place for the
    // token, which is closed below.
    if unsafe { OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &mut token) } == 0 {
        return false;
    }
    let mut elevation = TOKEN_ELEVATION { TokenIsElevated: 0 };
    let mut len = 0;
    // SAFETY: `elevation` is the size given.
    let ok = unsafe {
        GetTokenInformation(
            token,
            TokenElevation,
            (&raw mut elevation).cast(),
            size_of::<TOKEN_ELEVATION>() as u32,
            &mut len,
        )
    };
    // SAFETY: the token opened above.
    unsafe { CloseHandle(token) };
    ok != 0 && elevation.TokenIsElevated != 0
}

const UNINSTALL: &str = r"Software\Microsoft\Windows\CurrentVersion\Uninstall";
/// Caecilia 1.x's entry, made by Inno Setup.
const V1_KEY: &str = r"{7C1E9F2A-3B84-4C7D-9A1E-CAEC11A00001}_is1";

fn hive(scope: Scope) -> HKEY {
    match scope {
        Scope::Machine => HKEY_LOCAL_MACHINE,
        Scope::User => HKEY_CURRENT_USER,
    }
}

/// An open registry key, closed when dropped.
struct Key(HKEY);

impl Drop for Key {
    fn drop(&mut self) {
        // SAFETY: a key this module opened.
        unsafe { RegCloseKey(self.0) };
    }
}

fn status(code: u32) -> io::Result<()> {
    if code == ERROR_SUCCESS {
        Ok(())
    } else {
        Err(io::Error::from_raw_os_error(code as i32))
    }
}

impl Key {
    fn open(root: HKEY, path: &str) -> io::Result<Key> {
        Key::open_with(root, path, KEY_READ)
    }

    fn open_with(root: HKEY, path: &str, access: REG_SAM_FLAGS) -> io::Result<Key> {
        let mut key = ptr::null_mut();
        // SAFETY: a NUL-terminated path and a place for the key.
        status(unsafe {
            RegOpenKeyExW(
                root,
                wide(path).as_ptr(),
                0,
                access | KEY_WOW64_64KEY,
                &mut key,
            )
        })?;
        Ok(Key(key))
    }

    fn create(root: HKEY, path: &str) -> io::Result<Key> {
        let mut key = ptr::null_mut();
        // SAFETY: a NUL-terminated path, no class, no security attributes,
        // a place for the key.
        status(unsafe {
            RegCreateKeyExW(
                root,
                wide(path).as_ptr(),
                0,
                ptr::null(),
                REG_OPTION_NON_VOLATILE,
                KEY_WRITE | KEY_WOW64_64KEY,
                ptr::null(),
                &mut key,
                ptr::null_mut(),
            )
        })?;
        Ok(Key(key))
    }

    fn set_str(&self, name: &str, value: &str) -> io::Result<()> {
        let data = wide(value);
        // SAFETY: the data is the length given, NUL included.
        status(unsafe {
            RegSetValueExW(
                self.0,
                wide(name).as_ptr(),
                0,
                REG_SZ,
                data.as_ptr().cast(),
                (data.len() * 2) as u32,
            )
        })
    }

    fn set_dword(&self, name: &str, value: u32) -> io::Result<()> {
        // SAFETY: four bytes.
        status(unsafe {
            RegSetValueExW(
                self.0,
                wide(name).as_ptr(),
                0,
                REG_DWORD,
                (&raw const value).cast(),
                4,
            )
        })
    }

    fn get_str(&self, name: &str) -> Option<String> {
        let name = wide(name);
        let mut len = 0u32;
        // SAFETY: asking the length only.
        let code = unsafe {
            RegQueryValueExW(
                self.0,
                name.as_ptr(),
                ptr::null(),
                ptr::null_mut(),
                ptr::null_mut(),
                &mut len,
            )
        };
        if code != ERROR_SUCCESS || len == 0 {
            return None;
        }
        let mut buf = vec![0u16; (len as usize).div_ceil(2)];
        let mut ty = 0;
        // SAFETY: `buf` holds `len` bytes.
        let code = unsafe {
            RegQueryValueExW(
                self.0,
                name.as_ptr(),
                ptr::null(),
                &mut ty,
                buf.as_mut_ptr().cast(),
                &mut len,
            )
        };
        if code != ERROR_SUCCESS || ty != REG_SZ {
            return None;
        }
        let end = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
        Some(String::from_utf16_lossy(&buf[..end]))
    }
}

/// The installer's own executable quoted for a command line.
fn quoted(path: &Path) -> String {
    format!("\"{}\"", path.display())
}

/// Windows itself.
#[derive(Debug, Default)]
pub struct WinSystem;

impl System for WinSystem {
    fn installed(&self, scope: Scope, product: &str) -> Option<Installed> {
        let key = Key::open(hive(scope), &format!(r"{UNINSTALL}\{product}")).ok()?;
        Some(Installed {
            version: key.get_str("DisplayVersion")?,
            location: PathBuf::from(key.get_str("InstallLocation")?),
        })
    }

    fn register(&mut self, scope: Scope, e: &UninstallEntry) -> io::Result<()> {
        let key = Key::create(hive(scope), &format!(r"{UNINSTALL}\{}", e.name))?;
        let exe = quoted(&e.uninstaller);
        key.set_str("DisplayName", &e.name)?;
        key.set_str("DisplayVersion", &e.version)?;
        key.set_str("Publisher", &e.publisher)?;
        key.set_str("DisplayIcon", &e.icon.to_string_lossy())?;
        key.set_str("InstallLocation", &e.location.to_string_lossy())?;
        key.set_str("UninstallString", &format!("{exe} --uninstall"))?;
        key.set_str("QuietUninstallString", &format!("{exe} --uninstall /S"))?;
        key.set_str("ModifyPath", &exe)?;
        key.set_str(
            "URLInfoAbout",
            "https://github.com/alessonqueirozdev-hub/Caecilia",
        )?;
        key.set_str(
            "HelpLink",
            "https://github.com/alessonqueirozdev-hub/Caecilia",
        )?;
        key.set_str(
            "InstallDate",
            &crate::log::stamp(std::time::SystemTime::now())[..8],
        )?;
        key.set_dword("EstimatedSize", e.size_kib)?;
        key.set_dword("NoRepair", 0)?;
        Ok(())
    }

    fn unregister(&mut self, scope: Scope, product: &str) -> io::Result<()> {
        let parent = Key::open_with(hive(scope), UNINSTALL, KEY_ALL_ACCESS)?;
        // SAFETY: an open key and a NUL-terminated name.
        let code = unsafe { RegDeleteTreeW(parent.0, wide(product).as_ptr()) };
        match code {
            ERROR_SUCCESS => Ok(()),
            // ERROR_FILE_NOT_FOUND: already gone.
            2 => Ok(()),
            code => status(code),
        }
    }

    fn make_shortcut(&mut self, s: &Shortcut) -> io::Result<()> {
        shortcut(s).map_err(|e| io::Error::other(e.message()))
    }

    fn delete_at_restart(&mut self, path: &Path) -> bool {
        // SAFETY: a NUL-terminated path; no new name means delete. Only an
        // administrator may (it writes PendingFileRenameOperations).
        unsafe {
            MoveFileExW(
                wide(path).as_ptr(),
                ptr::null(),
                MOVEFILE_DELAY_UNTIL_REBOOT,
            ) != 0
        }
    }
}

fn shortcut(s: &Shortcut) -> windows::core::Result<()> {
    use windows::Win32::System::Com::{
        CLSCTX_INPROC_SERVER, COINIT_APARTMENTTHREADED, CoCreateInstance, CoInitializeEx,
        IPersistFile,
    };
    use windows::Win32::UI::Shell::{IShellLinkW, ShellLink};
    use windows::core::{HSTRING, Interface};

    // SAFETY: COM for this thread; a second call returns S_FALSE, harmless.
    let _ = unsafe { CoInitializeEx(None, COINIT_APARTMENTTHREADED) };
    // SAFETY: the system's shell link object.
    let link: IShellLinkW = unsafe { CoCreateInstance(&ShellLink, None, CLSCTX_INPROC_SERVER)? };
    let dir = s.target.parent().unwrap_or(Path::new(""));
    // SAFETY: each call takes a NUL-terminated string HSTRING provides.
    unsafe {
        link.SetPath(&HSTRING::from(s.target.as_os_str()))?;
        link.SetWorkingDirectory(&HSTRING::from(dir.as_os_str()))?;
        link.SetIconLocation(&HSTRING::from(s.icon.as_os_str()), 0)?;
        link.SetDescription(&HSTRING::from(s.description.as_str()))?;
        link.cast::<IPersistFile>()?
            .Save(&HSTRING::from(s.link.as_os_str()), true)?;
    }
    Ok(())
}

/// Shows `text` in a message box: for a person who ran the installer with
/// a mistake in its arguments, who has no console to read.
pub fn message(title: &str, text: &str) {
    use windows_sys::Win32::UI::WindowsAndMessaging::{MB_ICONWARNING, MB_OK, MessageBoxW};
    // SAFETY: two NUL-terminated strings, no owner window.
    unsafe {
        MessageBoxW(
            ptr::null_mut(),
            wide(text).as_ptr(),
            wide(title).as_ptr(),
            MB_OK | MB_ICONWARNING,
        )
    };
}

/// Whether Caecilia 1.x is installed (by its Inno Setup entry).
pub fn has_version_1() -> bool {
    Key::open(HKEY_LOCAL_MACHINE, &format!(r"{UNINSTALL}\{V1_KEY}")).is_ok()
}

/// `arg` quoted as the C runtime reads a command line.
pub fn quote_arg(arg: &str) -> String {
    if !arg.is_empty() && !arg.contains([' ', '\t', '"']) {
        return arg.to_owned();
    }
    let mut out = String::from('"');
    let mut backslashes = 0;
    for c in arg.chars() {
        match c {
            '\\' => backslashes += 1,
            '"' => {
                out.extend(std::iter::repeat_n('\\', backslashes * 2 + 1));
                out.push('"');
                backslashes = 0;
            }
            c => {
                out.extend(std::iter::repeat_n('\\', backslashes));
                out.push(c);
                backslashes = 0;
            }
        }
    }
    out.extend(std::iter::repeat_n('\\', backslashes * 2));
    out.push('"');
    out
}

/// A process started elevated.
#[derive(Debug)]
pub struct Elevated(HANDLE);

/// Why an elevated process did not start.
#[derive(Debug)]
pub enum ElevateError {
    /// The person said no to the administrator's prompt.
    Declined,
    /// Something else.
    Other(io::Error),
}

impl Elevated {
    /// Starts `exe` with `args` as an administrator: Windows asks the
    /// person first.
    pub fn start(exe: &Path, args: &[String]) -> Result<Elevated, ElevateError> {
        let file = wide(exe);
        let params = wide(
            args.iter()
                .map(|a| quote_arg(a))
                .collect::<Vec<_>>()
                .join(" "),
        );
        let verb = wide("runas");
        // SAFETY: an all-zero SHELLEXECUTEINFOW is valid; the strings
        // outlive the call.
        let mut info: SHELLEXECUTEINFOW = unsafe { std::mem::zeroed() };
        info.cbSize = size_of::<SHELLEXECUTEINFOW>() as u32;
        info.fMask = SEE_MASK_NOCLOSEPROCESS;
        info.lpVerb = verb.as_ptr();
        info.lpFile = file.as_ptr();
        info.lpParameters = params.as_ptr();
        info.nShow = 0; // SW_HIDE: the engine has no window.
        // SAFETY: `info` is filled as the call wants.
        if unsafe { ShellExecuteExW(&mut info) } == 0 {
            let e = io::Error::last_os_error();
            return Err(if e.raw_os_error() == Some(ERROR_CANCELLED as i32) {
                ElevateError::Declined
            } else {
                ElevateError::Other(e)
            });
        }
        if info.hProcess.is_null() {
            return Err(ElevateError::Other(io::Error::other("no process")));
        }
        Ok(Elevated(info.hProcess))
    }

    /// Its exit code, once it has ended.
    pub fn try_wait(&self) -> Option<i32> {
        // SAFETY: the process handle kept since it started.
        if unsafe { WaitForSingleObject(self.0, 0) } != WAIT_OBJECT_0 {
            return None;
        }
        let mut code = 0u32;
        // SAFETY: the same handle, and a place for the code.
        (unsafe { GetExitCodeProcess(self.0, &mut code) } != 0).then_some(code as i32)
    }
}

impl Drop for Elevated {
    fn drop(&mut self) {
        // SAFETY: the handle ShellExecuteExW gave.
        unsafe { CloseHandle(self.0) };
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn arguments_are_quoted_as_the_runtime_reads_them() {
        assert_eq!(quote_arg("plain"), "plain");
        assert_eq!(
            quote_arg("C:\\Program Files\\Caecilia"),
            "\"C:\\Program Files\\Caecilia\""
        );
        assert_eq!(
            quote_arg("D:\\dir with space\\"),
            "\"D:\\dir with space\\\\\""
        );
        assert_eq!(quote_arg("say \"hi\""), "\"say \\\"hi\\\"\"");
        assert_eq!(quote_arg(""), "\"\"");
    }

    #[test]
    fn a_shortcut_is_a_shell_link() {
        let dir =
            std::env::temp_dir().join(format!("caecilia-installer-lnk-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let link = dir.join("Caecilia.lnk");
        WinSystem
            .make_shortcut(&Shortcut {
                link: link.clone(),
                target: PathBuf::from(r"C:\Program Files\Caecilia\caecilia.exe"),
                icon: PathBuf::from(r"C:\Program Files\Caecilia\caecilia.ico"),
                description: "Caecilia".into(),
            })
            .unwrap();
        let bytes = std::fs::read(&link).unwrap();
        std::fs::remove_dir_all(&dir).unwrap();
        // The header of every .lnk: its size, 0x4C, then the link CLSID.
        assert_eq!(bytes[..4], [0x4C, 0, 0, 0]);
        assert_eq!(bytes[4..8], [0x01, 0x14, 0x02, 0x00]);
    }

    #[test]
    fn the_known_folders_are_found() {
        let p = places().unwrap();
        assert!(p.program_files.is_absolute() && p.common_files.starts_with(&p.program_files));
        assert!(p.user_programs.ends_with("Programs"));
    }
}
