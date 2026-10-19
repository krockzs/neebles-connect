//! NEEBLES Connect 1.0.0: governed graphical Open session.
//! Does not install or uninstall anything; Boss owns transactions and authenticated peer credentials.
use serde_json::{json, Value};
use std::fs::{self, File};
use std::io::{Read, Write};
use std::os::unix::net::UnixStream;
use std::os::unix::process::CommandExt;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, ExitCode};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{self, RecvTimeoutError};
use std::thread;
use std::time::{Duration, Instant};

const MODULE: &str = "neebles-connect";
const PROTOCOL: u64 = 1;
const MAX_FRAME: usize = 16 * 1024 * 1024;
const SOCKET: &str = "/run/neebles/modules.sock";
static STOP_REQUESTED: AtomicBool = AtomicBool::new(false);

#[cfg(target_os = "linux")]
unsafe extern "C" {
    fn prctl(option: i32, arg2: usize, arg3: usize, arg4: usize, arg5: usize) -> i32;
    fn signal(sig: i32, handler: extern "C" fn(i32)) -> usize;
    fn kill(pid: i32, sig: i32) -> i32;
}
extern "C" fn handle_signal(_signal: i32) { STOP_REQUESTED.store(true, Ordering::Relaxed); }

fn read_frame<R: Read>(stream: &mut R) -> Result<Value, String> {
    let mut length_bytes = [0u8; 4];
    stream.read_exact(&mut length_bytes).map_err(|e| format!("IPC header: {e}"))?;
    let length = u32::from_be_bytes(length_bytes) as usize;
    if length == 0 || length > MAX_FRAME { return Err(format!("IPC invalid frame size: {length}")); }
    let mut body = vec![0u8; length];
    stream.read_exact(&mut body).map_err(|e| format!("IPC body: {e}"))?;
    let msg: Value = serde_json::from_slice(&body).map_err(|e| format!("IPC invalid JSON: {e}"))?;
    if !msg.is_object() { return Err("IPC JSON is not an object".into()); }
    Ok(msg)
}
fn write_frame<W: Write>(stream: &mut W, obj: &Value) -> Result<(), String> {
    let b = serde_json::to_vec(obj).map_err(|e| format!("serialize IPC: {e}"))?;
    if b.is_empty() || b.len() > MAX_FRAME { return Err("IPC outgoing frame invalid length".into()); }
    stream.write_all(&(b.len() as u32).to_be_bytes()).map_err(|e| format!("write IPC header: {e}"))?;
    stream.write_all(&b).map_err(|e| format!("write IPC body: {e}"))?;
    Ok(())
}
fn check_identity(msg: &Value, session: &str) -> Result<(), String> {
    // Boss sometimes omits identity fields in a response. When present they MUST match.
    if let Some(id) = msg.get("module") { if id.as_str() != Some(MODULE) { return Err("foreign module identity".into()); } }
    if let Some(id) = msg.get("session_id") { if id.as_str() != Some(session) { return Err("foreign session identity".into()); } }
    Ok(())
}
fn check_message(msg: &Value, session: &str, wanted: &str) -> Result<(), String> {
    check_identity(msg,session)?;
    if msg.get("type").and_then(Value::as_str) != Some(wanted) {
        return Err(format!("Boss response is not {wanted}: {msg}"));
    }
    Ok(())
}
fn new_session() -> Result<String, String> {
    // Cryptographically random v4 session identifier via kernel, no host commands.
    let mut bytes = [0u8;16];
    File::open("/dev/urandom").and_then(|mut file| file.read_exact(&mut bytes))
        .map_err(|e| format!("random source unavailable: {e}"))?;
    bytes[6] = (bytes[6] & 0x0f) | 0x40;
    bytes[8] = (bytes[8] & 0x3f) | 0x80;
    Ok(format!("{:08x}-{:04x}-{:04x}-{:04x}-{:012x}",
        u32::from_be_bytes(bytes[0..4].try_into().unwrap()),
        u16::from_be_bytes(bytes[4..6].try_into().unwrap()),
        u16::from_be_bytes(bytes[6..8].try_into().unwrap()),
        u16::from_be_bytes(bytes[8..10].try_into().unwrap()),
        u64::from_be_bytes([0,0,bytes[10],bytes[11],bytes[12],bytes[13],bytes[14],bytes[15]])
    ))
}
fn module_root() -> Result<PathBuf, String> {
    let exe = std::env::current_exe().map_err(|e|format!("current_exe: {e}"))?;
    let runtime = exe.parent().ok_or("missing runtime parent")?;
    let root = runtime.parent().ok_or("missing module root")?;
    let manifest = root.join("manifest.json");
    let m:Value=serde_json::from_slice(&fs::read(&manifest).map_err(|e|format!("manifest read {manifest:?}: {e}"))?)
        .map_err(|e|format!("manifest JSON: {e}"))?;
    if m.get("name").and_then(Value::as_str)!=Some(MODULE) || m.get("version").and_then(Value::as_str)!=Some("1.0.0")
       || m.get("entrypoint").and_then(Value::as_str)!=Some("runtime/neebles-connect") {
        return Err("module identity/version/entrypoint mismatch".into());
    }
    Ok(root.to_path_buf())
}
fn desktop_binary(root: &Path) -> Result<PathBuf, String> {
    let path=root.join("bin/neebles-connect-desktop");
    let md=fs::symlink_metadata(&path).map_err(|e|format!("Qt binary missing: {e}"))?;
    if !md.file_type().is_file() || md.file_type().is_symlink() { return Err("Qt binary not a regular file".into()); }
    use std::os::unix::fs::PermissionsExt;
    if md.permissions().mode() & 0o111 == 0 { return Err("Qt binary not executable".into()); }
    let mut magic=[0u8;4];
    File::open(&path).and_then(|mut f|f.read_exact(&mut magic)).map_err(|e|e.to_string())?;
    if &magic!=b"\x7fELF" { return Err("Qt binary is not ELF".into()); }
    Ok(path)
}
fn launch_ui(root: &Path) -> Result<Child,String> {
    let binary=desktop_binary(root)?;
    let mut cmd=Command::new(&binary);
    cmd.current_dir(root);
    // Qt has no authority to administer Boss. Only the Rust parent uses Module IPC.
    cmd.env_remove("NEEBLES_MODULES_SOCKET");
    // Linux parent-death signal protects against orphan Qt processes when the
    // governed Rust parent is killed before its normal shutdown handler runs.
    #[cfg(target_os="linux")]
    unsafe { cmd.pre_exec(|| {
        if prctl(1,15,0,0,0)!=0 { return Err(std::io::Error::last_os_error()); }
        Ok(())
    }); }
    cmd.spawn().map_err(|e|format!("Qt process spawn failed: {e}"))
}
fn stop_ui(child: &mut Child) {
    if let Ok(Some(_))=child.try_wait(){return;}
    #[cfg(target_os="linux")]
    unsafe { let _=kill(child.id() as i32, 15); }
    let deadline=Instant::now()+Duration::from_secs(3);
    while Instant::now()<deadline {
        if let Ok(Some(_))=child.try_wait() {return;}
        thread::sleep(Duration::from_millis(50));
    }
    let _=child.kill();
    let _=child.wait();
}
// F26: module-owned installation gate, invoked only through authenticated
// CUSTOM V2 Construction (modules.runtime / rust-qt / session=true).
// The Secrets socket is a session-authorized readonly grant; the Secrets
// daemon confirms its canonical installed manifest via vault.status.installed.
// No host tool discovery, shell, master password or KDBX access here.
fn secrets_request(request: &Value) -> Result<Value,String> {
    let runtime=std::env::var_os("XDG_RUNTIME_DIR")
        .ok_or("NEEBLES Secrets requiere una sesión de usuario válida")?;
    let runtime=PathBuf::from(runtime);
    if !runtime.is_absolute() { return Err("XDG_RUNTIME_DIR no es absoluto".into()); }
    let socket=runtime.join("neebles-secrets/secrets.sock");
    let mut stream=UnixStream::connect(socket)
        .map_err(|_|"NEEBLES Connect requiere NEEBLES Secrets instalado y su servicio activo".to_string())?;
    stream.set_read_timeout(Some(Duration::from_secs(35))).map_err(|e|e.to_string())?;
    stream.set_write_timeout(Some(Duration::from_secs(35))).map_err(|e|e.to_string())?;
    write_frame(&mut stream,request)?;
    let response=read_frame(&mut stream)?;
    if response.get("ok").and_then(Value::as_bool)!=Some(true) {
        let error=response.get("error").and_then(Value::as_str).unwrap_or("unknown");
        return Err(format!("NEEBLES Secrets rechazó la operación: {error}"));
    }
    response.get("data").cloned().ok_or_else(|| "NEEBLES Secrets no devolvió data".to_string())
}
fn prepare_vault_install() -> Result<(),String> {
    // The user prepares and unlocks Secrets before installing Connect.
    // Boss will own generic dependency preflight in a later release.
    // Only Secrets may authorize or create the module-owned KDBX.
    module_root()?;
    let created=secrets_request(&json!({"op":"vault.ensure","module":MODULE}))?;
    if created.get("module").and_then(Value::as_str)!=Some(MODULE)
       || created.get("exists").and_then(Value::as_bool)!=Some(true) {
        return Err("Secrets did not confirm the Connect vault".into());
    }
    Ok(())
}

fn run() -> Result<(),String> {
    let arguments: Vec<String> = std::env::args().skip(1).collect();
    if arguments.len()==2 && arguments[0]=="--intent" && arguments[1]=="install-vault" {
        return prepare_vault_install();
    }
    if arguments.len()!=2 || arguments[0]!="--intent" || arguments[1]!="open" {return Err("usage: neebles-connect --intent open | install-vault".into());}
    if std::env::var("NEEBLES_MODULE").unwrap_or_else(|_|MODULE.to_string())!=MODULE {
        return Err("Governor module identity mismatch".into());
    }
    let root=module_root()?;
    desktop_binary(&root)?; // fail before connecting to Boss
    let session=new_session()?;
    let socket_path=std::env::var("NEEBLES_MODULES_SOCKET").unwrap_or_else(|_|SOCKET.into());
    if !socket_path.starts_with('/') {return Err("Module IPC socket must be absolute".into());}
    let mut writer=UnixStream::connect(&socket_path).map_err(|e|format!("cannot connect Boss Module IPC: {e}"))?;
    writer.set_read_timeout(Some(Duration::from_secs(12))).map_err(|e|e.to_string())?;
    write_frame(&mut writer,&json!({"type":"register","protocol":PROTOCOL,"module":MODULE,"session_id":session,"endpoints":{"commands":[]}}))?;
    let registered=read_frame(&mut writer)?;
    check_message(&registered,&session,"registered")?;
    if registered.get("protocol").and_then(Value::as_u64)!=Some(PROTOCOL) {return Err("unexpected IPC protocol".into());}
    let topics=["module.lifecycle", "settings.neebles-connect"];
    write_frame(&mut writer,&json!({"type":"subscribe","module":MODULE,"session_id":session,"topics":topics}))?;
    let subscribed=read_frame(&mut writer)?;
    check_message(&subscribed,&session,"subscribed")?;
    let accepted=subscribed.get("topics").and_then(Value::as_array).ok_or("missing subscription topics")?;
    if accepted.len()!=topics.len() || !topics.iter().all(|t|accepted.iter().any(|s|s.as_str()==Some(t))) {
        return Err("Boss did not accept the declared subscriptions".into());
    }
    writer.set_read_timeout(None).map_err(|e|e.to_string())?;
    // Receive on a separate thread: no partial-frame corruption when checking child process exit.
    let mut reader=writer.try_clone().map_err(|e|e.to_string())?;
    let (tx,rx)=mpsc::channel::<Result<Value,String>>();
    thread::spawn(move || loop {
        let msg=read_frame(&mut reader);
        let done=msg.is_err();
        if tx.send(msg).is_err() || done {break;}
    });
    #[cfg(target_os="linux")]
    unsafe { signal(15,handle_signal); signal(2,handle_signal); }
    let mut ui=launch_ui(&root)?;
    let mut should_unregister=true;
    let mut error:Option<String>=None;
    loop {
        if STOP_REQUESTED.load(Ordering::Relaxed) {break;}
        match ui.try_wait() {
            Ok(Some(_)) => break,
            Ok(None) => {},
            Err(e) => {error=Some(format!("Qt child status: {e}")); break;}
        }
        match rx.recv_timeout(Duration::from_millis(150)) {
            Ok(Ok(msg)) => {
                if let Err(e)=check_identity(&msg,&session) {error=Some(e);break;}
                match msg.get("type").and_then(Value::as_str) {
                    Some("ping") => {
                        if let Err(e)=write_frame(&mut writer,&json!({"type":"pong","module":MODULE,"session_id":session})) {error=Some(e);break;}
                    },
                    Some("shutdown") => {
                        if let Err(e)=write_frame(&mut writer,&json!({"type":"shutdown_ack","module":MODULE,"session_id":session})) {error=Some(e);}
                        should_unregister=false;
                        break;
                    },
                    Some("event") | Some("notification_closed") => {},
                    Some("error") => {error=Some(format!("Boss IPC error: {msg}"));break;},
                    _ => {error=Some(format!("unexpected Boss IPC message: {msg}"));break;},
                }
            },
            Ok(Err(e)) => { error=Some(e);break; },
            Err(RecvTimeoutError::Timeout) => {},
            Err(RecvTimeoutError::Disconnected) => {error=Some("Boss IPC connection closed".into());break;}
        }
    }
    stop_ui(&mut ui);
    if should_unregister {
        if let Err(e)=write_frame(&mut writer,&json!({"type":"unregister","module":MODULE,"session_id":session,"reason":"runtime_exit"})) {
            if error.is_none() {error=Some(e);}
        }
    }
    if let Some(e)=error {return Err(e);}
    Ok(())
}
fn main() -> ExitCode {
    match run() {Ok(())=>ExitCode::SUCCESS,Err(e)=>{eprintln!("NEEBLES Connect runtime: {e}");ExitCode::FAILURE}}
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test] fn ipc_roundtrip_and_big_endian(){
        let mut b=Vec::new();let obj=json!({"type":"ping","module":MODULE,"session_id":"abc"});
        write_frame(&mut b,&obj).unwrap();
        assert_eq!(u32::from_be_bytes(b[0..4].try_into().unwrap()) as usize,b.len()-4);
        assert_eq!(read_frame(&mut b.as_slice()).unwrap(),obj);
    }
    #[test] fn refuse_zero_or_oversized_frames() {
        assert!(read_frame(&mut &[0,0,0,0][..]).is_err());
        let len=(MAX_FRAME as u32)+1;assert!(read_frame(&mut len.to_be_bytes().as_slice()).is_err());
    }
    #[test] fn refuse_foreign_sessions() {
        assert!(check_identity(&json!({"type":"ping","module":"other"}),"x").is_err());
        assert!(check_identity(&json!({"type":"ping","session_id":"else"}),"x").is_err());
        assert!(check_identity(&json!({"type":"ping","module":MODULE,"session_id":"x"}),"x").is_ok());
    }
    #[test] fn refuse_wrong_register_ack(){
        assert!(check_message(&json!({"type":"failed"}),"x","registered").is_err());
        assert!(check_message(&json!({"type":"registered","module":"other"}),"x","registered").is_err());
    }
    #[test] fn session_is_v4_uuid() {
        let s=new_session().unwrap();assert_eq!(s.len(),36);assert_eq!(&s[14..15],"4");
    }
}
