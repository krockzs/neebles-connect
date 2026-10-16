#!/usr/bin/env python3
"""NEEBLES Connect F7: controlled Rust offline compilation and explicit Boss contract promotion.
Does not install into /opt, call Boss, change CUSTOM or publish Git.
"""
from __future__ import annotations
import argparse, hashlib, json, os, re, shutil, stat, struct, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MODULE = 'neebles-connect'
QT_SHA = 'edd2a86a85b9576c285e6ca310d3d72f30699b015714e31a3a624e81efce5a1f'
CACHE = Path.home()/'.cache/neebles/connect-f7-builds'
BASE_LIFECYCLE = '701525ae14ff56cf8ff4445759ed4102c7f24736'
BASE_MANIFEST = '80980b2b6759b639f1eba198eee8730cd7ff4d60'
SRC = ['runtime/src/main.rs','runtime/Cargo.toml','runtime/Cargo.lock']
OPEN_OP = {'artillery':'boss.workspace_execution','objective':'construction.step','munition':{'step':'open-runtime'},'tactics':{},'intelligence':{}}
UNINSTALL = {'schema':1,'hardcoded':{},'main':'governor','extras':[],
             'cleanup':[{'id':'local-settings','type':'settings','ref':'manifest.settings','policy':'user-choice'}],
             'failure':{'unknown_process_owner':'abort','resource_still_running':'abort','cleanup_error':'abort'}}

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def blobsha(p):
    b=p.read_bytes();return hashlib.sha1(b'blob '+str(len(b)).encode()+b'\0'+b).hexdigest()
def safe(rel):
    p=ROOT
    for seg in Path(rel).parts:
        if seg in ('','..','.') or seg.startswith('/'):raise RuntimeError('invalid path '+rel)
        p=p/seg
        if p.is_symlink():raise RuntimeError('symlink not allowed: '+str(p))
    return p

def canonical(j):return (json.dumps(j,ensure_ascii=False,indent=2)+'\n').encode()
def base_contracts():
    m=json.loads(safe('manifest.json').read_text()); l=json.loads(safe('lifecycle.json').read_text())
    if (m.get('schema'),m.get('name'),m.get('version'),m.get('entrypoint'),m.get('contracts'))!=(4,MODULE,'1.0.0','runtime/neebles-connect',[]):
        raise RuntimeError('manifest identity/contracts not from F6')
    if 'tray' in m or m.get('surfaces')!='surfaces.json': raise RuntimeError('unexpected tray or surfaces contract')
    if json.loads(safe('surfaces.json').read_text()).get('items')!={}:raise RuntimeError('surfaces must remain empty in V1')
    if safe('manifest.json').is_symlink() or safe('lifecycle.json').is_symlink():raise RuntimeError('symlink contracts')
    if 'governor.open' not in l.get('hardcoded',{}):
        if blobsha(safe('manifest.json'))!=BASE_MANIFEST or blobsha(safe('lifecycle.json'))!=BASE_LIFECYCLE:
            raise RuntimeError('baseline contracts locally modified: inspect manually, no overwrite')
        state='baseline'
    else:
        state='promoted'
    return m,l,state

def desired(m,l):
    nm=dict(m); nm['uninstall']=UNINSTALL
    nl=json.loads(json.dumps(l))
    nl['hardcoded']['governor.open']='open'
    nl['transitions']['open']={'operations':{'launch-runtime':OPEN_OP}}
    return nm,nl

def inspect():
    if not (ROOT/'.git').is_dir() or ROOT.is_symlink():raise RuntimeError('missing real Connect checkout')
    if sha(safe('bin/neebles-connect-desktop'))!=QT_SHA:raise RuntimeError('F6 Qt SHA mismatch')
    if not all(safe('assets/'+a).is_file() for a in ('neebles-connect-icon.png','neebles-connect-logo.png','neebles-connect-tray-icon.png')):raise RuntimeError('missing official assets')
    if not all(safe(x).is_file() for x in SRC):raise RuntimeError('F7 source not installed')
    m,l,state=base_contracts()
    if state=='promoted':
        dm,dl=desired(m,l)
        if m!=dm or l!=dl:raise RuntimeError('promoted contracts diverge')
        if not safe('runtime/neebles-connect').is_file():raise RuntimeError('promoted Rust ELF missing')
    return state

def tools():
    rustc=Path(shutil.which('rustc') or '/nonexistent')
    cargo=Path(shutil.which('cargo') or '/nonexistent')
    if not rustc.is_file() or not cargo.is_file():raise RuntimeError('rustc/cargo missing. Restore approved Rust 1.98.1 toolchain first')
    def version(p):return subprocess.run([str(p),'--version'],capture_output=True,text=True,timeout=20,check=True).stdout.strip()
    rv=version(rustc);cv=version(cargo)
    if not re.match(r'^rustc 1\.98\.1\b',rv) or not re.match(r'^cargo 1\.98\.1\b',cv):
        raise RuntimeError('Rust toolchain mismatch: '+rv+' / '+cv+'; no other version fallback')
    return cargo,rv,cv

def check_elf(p):
    b=p.read_bytes()
    if b[:6]!=b'\x7fELF\x02\x01' or b[16:18] not in (b'\x02\x00',b'\x03\x00') or b[18:20]!=b'\x3e\x00':
        raise RuntimeError('runtime is not dynamically linked x86_64 ELF')
    if not p.stat().st_mode&stat.S_IXUSR:raise RuntimeError('runtime ELF is not executable')

def latest_info():
    p=CACHE/'latest.json'
    d=json.loads(p.read_text())
    cand=Path(d['candidate'])
    if cand != CACHE/'release/neebles-connect':raise RuntimeError('candidate path must remain in controlled cache')
    if not cand.is_file() or sha(cand)!=d['sha256'] or d['qt_sha256']!=QT_SHA:raise RuntimeError('F7 candidate has changed')
    if {x:sha(safe(x)) for x in SRC}!=d['sources']:raise RuntimeError('F7 source drift after compilation')
    check_elf(cand)
    return cand,d

def write_atomic(p, data, mode=None):
    p.parent.mkdir(parents=True,exist_ok=True)
    fd,name=tempfile.mkstemp(prefix='.f7-',dir=p.parent)
    try:
        with os.fdopen(fd,'wb') as f:
            f.write(data);f.flush();os.fsync(f.fileno())
        os.chmod(name,mode if mode is not None else (stat.S_IMODE(p.stat().st_mode) if p.exists() else 0o644))
        os.replace(name,p)
    finally:
        if os.path.exists(name):os.unlink(name)

def main():
    a=argparse.ArgumentParser();g=a.add_mutually_exclusive_group(required=True)
    for option in ('preview','build','promote','verify'):g.add_argument('--'+option,action='store_true')
    args=a.parse_args();state=inspect()
    if args.preview:
        print('F7 PREVIEW: Connect sources and F6 Qt SHA OK; contracts '+state)
        print('NEEDED: offline pinned Rust 1.98.1; no host Qt, no Tray; candidate in '+str(CACHE))
        return
    if args.build:
        cargo,rv,cv=tools()
        print('F7 TOOLCHAIN:',rv,'/',cv,flush=True)
        CACHE.mkdir(parents=True,exist_ok=True)
        log=CACHE/'BUILD_F7.log'
        env=os.environ.copy();env.update({'CARGO_NET_OFFLINE':'true','CARGO_TARGET_DIR':str(CACHE/'target'),'CARGO_INCREMENTAL':'0'})
        with log.open('w') as out:
            for cmd in ([str(cargo),'test','--locked','--offline','--release','--manifest-path',str(safe('runtime/Cargo.toml'))],
                        [str(cargo),'build','--locked','--offline','--release','--manifest-path',str(safe('runtime/Cargo.toml'))]):
                out.write('COMMAND: '+' '.join(cmd)+'\n');out.flush()
                r=subprocess.run(cmd,cwd=ROOT,env=env,stdout=out,stderr=subprocess.STDOUT)
                if r.returncode!=0:raise RuntimeError(f'F7 cargo failed ({r.returncode}); see {log}')
        built=CACHE/'target/release/neebles-connect';check_elf(built)
        cand=CACHE/'release/neebles-connect'; cand.parent.mkdir(exist_ok=True)
        write_atomic(cand,built.read_bytes(),0o755)
        d={'version':'1.0.0','module':MODULE,'candidate':str(cand),'sha256':sha(cand),'qt_sha256':QT_SHA,
           'sources':{x:sha(safe(x)) for x in SRC},'rustc':rv,'cargo':cv,'tests':'cargo test --locked --offline --release passed',
           'status':'BUILD_GATE_ONLY_NOT_RUNTIMELEASE_CERTIFIED'}
        write_atomic(CACHE/'latest.json',canonical(d))
        print('F7 BUILD OK (not Boss certified):',d['sha256'])
        print('F7 LOG:',log)
        return
    cand,d=latest_info()
    m,l,_=base_contracts();dm,dl=desired(m,l)
    dest=safe('runtime/neebles-connect')
    if args.promote:
        if state=='promoted':
            if sha(dest)!=d['sha256']:raise RuntimeError('promoted binary differs; refuse overwrite')
            print('F7 PROMOTE: already applied; no changes');return
        if dest.exists():raise RuntimeError('unexpected existing Rust entrypoint, refuse overwrite')
        backup=CACHE/'promotion-backup'
        if backup.exists():raise RuntimeError('previous F7 backup exists, inspect before promote')
        backup.mkdir(parents=True)
        shutil.copy2(safe('manifest.json'),backup/'manifest.json')
        shutil.copy2(safe('lifecycle.json'),backup/'lifecycle.json')
        # Source ELF and contract promotion is recoverable using exact backed-up originals.
        write_atomic(dest,cand.read_bytes(),0o755)
        write_atomic(safe('manifest.json'),canonical(dm))
        write_atomic(safe('lifecycle.json'),canonical(dl))
        print('F7 PROMOTE OK: executable Rust, manifest.uninstall, lifecycle.open staged locally.')
        print('Backup:',backup)
    if args.verify or args.promote:
        if inspect()!='promoted' or sha(dest)!=d['sha256']:
            raise RuntimeError('F7 verification failed')
        print('F7 VERIFY OK: runtime ELF sha='+d['sha256'])
        print('IMPORTANT: Boss installation NOT certified; CUSTOM RootFS/Construction pending.')

if __name__=='__main__':
    try:main()
    except (RuntimeError,OSError,ValueError,subprocess.SubprocessError) as e:
        print('F7 BLOQUEADO:',e,file=sys.stderr);sys.exit(3)
