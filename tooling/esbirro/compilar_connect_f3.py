#!/usr/bin/env python3
"""F3 link-hotfix: reproducible *candidate* from git-visible Connect sources + pinned CUSTOM DEBs.

No sudo, network, host Qt/pkg-config fallback, repository writes or Boss launch.
`--build` writes only under ~/.cache/neebles/connect-f3-builds/.
COMPILATION DOES NOT IMPLY ELF closure, world certification or Boss install.
"""
from __future__ import annotations
import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

SOURCE_FILES = ('ui/desktop/main.cpp','ui/desktop/MainWindow.cpp','ui/desktop/MainWindow.h',
    'ui/desktop/VncPane.cpp','ui/desktop/VncPane.h','ui/desktop/SshPane.cpp',
    'ui/desktop/SshPane.h','ui/desktop/SshTerminal.cpp','ui/desktop/SshTerminal.h',
    'ui/desktop/CMakeLists.txt')
SASL = ('libsasl2-dev','2.1.28+dfsg1-9','ffb10079c52733a6073f1e0f5df88f19ca8e89dd959edee725515b0075391851',
        'runtime/modules/packages/libsasl2-dev_2.1.28+dfsg1-9_amd64.deb')

# Actual runtime closures from the authenticated Connect CUSTOM V2 membership.
# Never pull these libraries from /usr/lib of the host or a Downloads overlay.
# Tuple: Debian name, exact Debian version, CUSTOM TSV SHA256, physical filename, ELF soname.
VNC_LINK_CLOSURE = (
    ('liblzo2-2', '2.10-3+b1', 'f3032201fe2928a87e13f05ce256ff3ac2a7860c7e594dc51ae6d7348a4466ce', 'liblzo2-2_2.10-3+b1_amd64.deb', 'liblzo2.so.2'),
    ('libgcrypt20', '1.11.0-7+deb13u1', 'f0eb41f8cb40896e0822e9ab65611e8383c12b721c76c54764f744abb4a8c247', 'libgcrypt20_1.11.0-7+deb13u1_amd64.deb', 'libgcrypt.so.20'),
    ('libgpg-error0', '1.51-4', '22b95570fd41c113ef6f5651563b4d748292844baab1278a46eb940c1dec2322', 'libgpg-error0_1.51-4_amd64.deb', 'libgpg-error.so.0'),
)

def check_custom_membership(tsv: Path) -> None:
    """Fail closed unless the three pins exist *exactly once* in CUSTOM V2."""
    if tsv.is_symlink() or not tsv.is_file():
        raise RuntimeError('Falta manifest de membresía CUSTOM V2: ' + str(tsv))
    entries = {}
    for line in tsv.read_text(encoding='utf-8').splitlines():
        if not line or line.startswith('#'):
            continue
        parts = line.split('\t')
        if len(parts) != 5:
            raise RuntimeError('TSV CUSTOM invalido: ' + line[:100])
        entries.setdefault(parts[0], []).append(parts)
    for name, version, digest, filename, _soname in VNC_LINK_CLOSURE:
        expected = [name, version, 'amd64', filename, digest]
        if entries.get(name) != [expected]:
            raise RuntimeError('Membership CUSTOM distinta/duplicada para ' + name)


NEED_OVERLAY = (
    'usr/include/rfb/rfbclient.h', 'usr/include/sasl/sasl.h',
    'usr/include/libssh2.h', 'usr/include/vterm.h',
    'usr/lib/x86_64-linux-gnu/libvncclient.so.1',
    'usr/lib/x86_64-linux-gnu/libssh2.so.1',
    'usr/lib/x86_64-linux-gnu/libvterm.so.0',
    'usr/lib/x86_64-linux-gnu/liblzo2.so.2',
    'usr/lib/x86_64-linux-gnu/libgcrypt.so.20',
    'usr/lib/x86_64-linux-gnu/libgpg-error.so.0',
)

def sha(path: Path) -> str:
    h=hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda:f.read(1<<20), b''):h.update(b)
    return h.hexdigest()

def deb_field(path:Path,key:str)->str:
    c=subprocess.run(['dpkg-deb','-f',str(path),key],capture_output=True,text=True,timeout=30,check=True)
    v=c.stdout.strip()
    if not v or '\n' in v or '\r' in v:raise RuntimeError('Metadato inválido '+key+': '+str(path))
    return v

def check_deb(path:Path, name:str, version:str, digest:str)->None:
    if not path.is_file() or path.is_symlink():raise RuntimeError('Falta material CUSTOM o es un symlink: '+str(path))
    if sha(path)!=digest:raise RuntimeError('SHA256 incorrecto '+name+': '+str(path))
    actual=[deb_field(path,k) for k in ('Package','Version','Architecture')]
    if actual != [name,version,'amd64']:raise RuntimeError('DEB incompatible '+name+': '+repr(actual))

def audit(connect:Path, custom:Path)->list[dict]:
    if not (connect/'.git').is_dir() or not (custom/'.git').is_dir():raise RuntimeError('Requiere checkouts Git Connect/CUSTOM')
    if json.loads((connect/'manifest.json').read_text())['version']!='1.0.0':raise RuntimeError('Version Connect inesperada')
    f2=json.loads((connect/'tooling/esbirro/f2-source-lock.json').read_text())
    f3=json.loads((connect/'tooling/esbirro/f3-source-lock.json').read_text())
    past=json.loads((connect/'tooling/esbirro/m6.1-source-lock.json').read_text())
    for name, digest in past['files'].items():
        expected=f3['migrated_files'].get(name, f2['migrated_files'].get(name,digest))
        if sha(connect/name)!=expected:raise RuntimeError('Fuente M6.1/F3 alterada: '+name)
    if sha(connect/'ui/desktop/CMakeLists.txt') != f3['migrated_files']['ui/desktop/CMakeLists.txt']:
        raise RuntimeError('CMake F3 alterado')
    import importlib.util
    audit_f2=connect/'tooling/esbirro/auditar_cierre_f2.py'
    spec=importlib.util.spec_from_file_location('neebles_f2_audit',audit_f2)
    if spec is None or spec.loader is None:raise RuntimeError('F2 auditor missing')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    items=[]
    for name,(version,digest,relative,role) in module.DEBS.items():
        check_deb(custom/relative,name,version,digest)
        items.append(dict(package=name,version=version,sha256=digest,path=str(custom/relative),role=role))
        print('OK PACKAGE',name,version,digest[:18])
    name,version,digest,rel=SASL
    check_deb(custom/rel,name,version,digest)
    items.append(dict(package=name,version=version,sha256=digest,path=str(custom/rel),role='build-only'))
    print('OK PACKAGE',name,version,digest[:18],'(SHA Debian verificado)')
    check_custom_membership(custom/'runtime/manifests/modules/neebles-connect.packages.tsv')
    for name, version, digest, filename, soname in VNC_LINK_CLOSURE:
        package = custom/'runtime/modules/packages'/filename
        check_deb(package, name, version, digest)
        items.append(dict(package=name,version=version,sha256=digest,path=str(package),role='runtime-existing'))
        print('OK LINK CLOSURE',name,version,soname,digest[:18])
    sysroot=custom/'build_sysroot_6.8.2'
    needed={
        'Qt6 Config':sysroot/'usr/lib/x86_64-linux-gnu/cmake/Qt6/Qt6Config.cmake',
        'Qt6 Widgets 6.8.2':sysroot/'usr/lib/x86_64-linux-gnu/libQt6Widgets.so.6.8.2',
        'GCC 14':sysroot/'usr/bin/x86_64-linux-gnu-g++-14',
        'GCC builtin include':sysroot/'usr/lib/gcc/x86_64-linux-gnu/14/include',
        'C++ std headers':sysroot/'usr/include/c++/14',
    }
    for label,path in needed.items():
        if not path.exists():raise RuntimeError('No está en sysroot: '+label+' '+str(path))
        print('OK ESBIRRO',label)
    if not os.access(needed['GCC 14'],os.X_OK):raise RuntimeError('GCC controlado no ejecutable')
    for rel in SOURCE_FILES:
        if not (connect/rel).is_file():raise RuntimeError('Falta fuente '+rel)
    return items

def _merge_root(extracted:Path,dest:Path)->None:
    # Only usr/include and usr/lib from a controlled development overlay.
    # Each .deb was validated by SHA256 and dpkg-deb before extracting.
    for suffix in ('usr/include','usr/lib'):
        root=extracted/suffix
        if not root.is_dir():continue
        for s in sorted(root.rglob('*')):
            rel=s.relative_to(extracted)
            t=dest/rel
            if s.is_dir() and not s.is_symlink():
                if t.is_symlink() or (t.exists() and not t.is_dir()):raise RuntimeError('Overlay directory conflict: '+str(rel))
                t.mkdir(parents=True,exist_ok=True)
            elif s.is_symlink():
                value=os.readlink(s)
                if os.path.isabs(value) or '..' in Path(value).parts:
                    raise RuntimeError('Symlink outside overlay: '+str(rel)+' -> '+value)
                if t.is_symlink():
                    if os.readlink(t)!=value:raise RuntimeError('Symlink conflict: '+str(rel))
                elif t.exists():raise RuntimeError('Symlink/file conflict: '+str(rel))
                else:
                    t.parent.mkdir(parents=True,exist_ok=True);os.symlink(value,t)
            elif s.is_file():
                if t.is_symlink() or (t.exists() and (not t.is_file() or sha(t)!=sha(s))):
                    raise RuntimeError('Overlay file conflict: '+str(rel))
                if not t.exists():
                    t.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(s,t)
            else:raise RuntimeError('Unsupported DEB item '+str(rel))

def make_overlay(items:list[dict],candidate:Path)->Path:
    overlay=candidate/'overlay';overlay.mkdir(mode=0o700)
    isolated=candidate/'extracted';isolated.mkdir(mode=0o700)
    for item in items:
        src=Path(item['path']); target=isolated/item['package'];target.mkdir(mode=0o700)
        r=subprocess.run(['dpkg-deb','-x',str(src),str(target)],capture_output=True,text=True)
        if r.returncode:raise RuntimeError('DEB extract failed '+item['package']+': '+r.stderr[-500:])
        _merge_root(target,overlay)
        print('STAGED',item['package'],flush=True)
    for rel in NEED_OVERLAY:
        p=overlay/rel
        if not p.is_file():raise RuntimeError('Overlay incompleto: '+str(p))
        if not p.resolve().is_relative_to(overlay.resolve()):raise RuntimeError('Overlay lib points outside: '+str(p))
    return overlay

def flags(sysroot:Path)->str:
    s=sysroot.as_posix()
    return ' '.join((
        f'-B{s}/usr/libexec/gcc/x86_64-linux-gnu/14/',
        f'-B{s}/usr/lib/gcc/x86_64-linux-gnu/14/',
        f'-B{s}/usr/bin/', '-nostdinc','-nostdinc++',
        f'-isystem {s}/usr/include/c++/14',
        f'-isystem {s}/usr/include/x86_64-linux-gnu/c++/14',
        f'-isystem {s}/usr/lib/gcc/x86_64-linux-gnu/14/include',
        f'-isystem {s}/usr/include/x86_64-linux-gnu',
        f'-isystem {s}/usr/include',
    ))

def _run(cmd:list[str],log:Path,env:dict)->None:
    print('RUN',cmd[0],cmd[1] if len(cmd)>1 else '',flush=True)
    with log.open('a') as f:
        f.write('COMMAND: '+subprocess.list2cmdline(cmd)+'\n');f.flush()
        code=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT).returncode
    if code:
        lines=log.read_text(errors='replace').splitlines()
        print('\n'.join(lines[-50:]),flush=True)
        raise RuntimeError('Build phase failed ('+str(code)+') '+str(log))

def _cache_guards(build:Path,sysroot:Path)->None:
    expected={
      'CMAKE_CXX_COMPILER':str(sysroot/'usr/bin/x86_64-linux-gnu-g++-14'),
      'CMAKE_SYSROOT':str(sysroot),
      'CMAKE_FIND_ROOT_PATH':str(sysroot),
      'Qt6_DIR':str(sysroot/'usr/lib/x86_64-linux-gnu/cmake/Qt6'),
      'Qt6Widgets_DIR':str(sysroot/'usr/lib/x86_64-linux-gnu/cmake/Qt6Widgets'),
      'CMAKE_CXX_FLAGS':flags(sysroot),
    }
    seen={}
    for line in (build/'CMakeCache.txt').read_text().splitlines():
        if ':' in line and '=' in line and not line.startswith(('#','//')):
            k=line.split(':',1)[0]
            if k in expected:seen.setdefault(k,[]).append(line.split('=',1)[1])
    for k,v in expected.items():
        if seen.get(k)!=[v]:raise RuntimeError('CMake provenance invalid: '+k+' expected '+v+' seen '+repr(seen.get(k)))
        print('PROVENANCE OK',k,flush=True)

def _needed(binary:Path)->list[str]:
    r=subprocess.run(['readelf','-d',str(binary)],check=True,capture_output=True,text=True)
    needed=[]
    for line in r.stdout.splitlines():
        if '(NEEDED)' in line and '[' in line:needed.append(line.split('[',1)[1].split(']',1)[0])
        if '(RPATH)' in line or '(RUNPATH)' in line:raise RuntimeError('RPATH/RUNPATH not allowed in ELF')
    return needed

def _elf_inventory(binary:Path,overlay:Path,sysroot:Path)->list[str]:
    search=[overlay/'usr/lib/x86_64-linux-gnu', sysroot/'usr/lib/x86_64-linux-gnu',
        sysroot/'lib/x86_64-linux-gnu',sysroot/'usr/lib',sysroot/'lib']
    missing=[]
    for item in _needed(binary):
        p=next((d/item for d in search if (d/item).is_file()),None)
        if p is None:missing.append(item)
        print(('FOUND' if p else 'NOT FOUND'),'ELF NEEDED',item,flush=True)
    return missing

def build(connect:Path,custom:Path,items:list[dict])->Path:
    for command in ('cmake','readelf','dpkg-deb'):
        if shutil.which(command) is None:raise RuntimeError('Missing utility '+command)
    stage_base=Path.home()/'.cache/neebles/connect-f3-builds'
    stage_base.mkdir(parents=True,exist_ok=True)
    candidate=stage_base/('candidate_'+datetime.now().strftime('%Y%m%d_%H%M%S_%f'))
    candidate.mkdir(mode=0o700)
    (candidate/'build').mkdir();(candidate/'install-stage').mkdir()
    log=candidate/'BUILD.log'
    print('CANDIDATE',candidate,flush=True)
    overlay=make_overlay(items,candidate)
    sysroot=custom/'build_sysroot_6.8.2';b=candidate/'build'
    env=os.environ.copy()
    for k in ('CXX','CC','CMAKE_PREFIX_PATH','CMAKE_MODULE_PATH','Qt6_DIR',
              'PKG_CONFIG_PATH','PKG_CONFIG_LIBDIR','PKG_CONFIG_SYSROOT_DIR',
              'LD_LIBRARY_PATH','LIBRARY_PATH','CPATH','CPLUS_INCLUDE_PATH',
              'C_INCLUDE_PATH','QTDIR','QT_PLUGIN_PATH','CMAKE_TOOLCHAIN_FILE'):
        env.pop(k,None)
    env['PKG_CONFIG_LIBDIR']=str(candidate/'no-host-pkgconfig')
    Qt=sysroot/'usr/lib/x86_64-linux-gnu/cmake/Qt6'
    compiler=sysroot/'usr/bin/x86_64-linux-gnu-g++-14'
    args=['cmake','-S',str(connect/'ui/desktop'),'-B',str(b),
        '-DCMAKE_BUILD_TYPE=Release',
        '-DCMAKE_INSTALL_PREFIX=/opt/neebles/modules/neebles-connect',
        '-DCMAKE_SKIP_BUILD_RPATH=ON','-DCMAKE_SKIP_INSTALL_RPATH=ON',
        '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
        f'-DCMAKE_CXX_COMPILER={compiler}',f'-DCMAKE_SYSROOT={sysroot}',
        f'-DCMAKE_FIND_ROOT_PATH={sysroot}',
        '-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY',
        '-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY',
        '-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY',
        '-DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER',
        '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=FALSE',
        '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=FALSE',
        f'-DQt6_DIR={Qt}',f'-DCMAKE_CXX_FLAGS={flags(sysroot)}',
        f'-DNEEBLES_CONNECT_CONTROLLED_SYSROOT={sysroot}',
        f'-DNEEBLES_CONNECT_DEP_OVERLAY={overlay}']
    (candidate/'SOURCE_SHA256.json').write_text(json.dumps({r:sha(connect/r) for r in SOURCE_FILES},indent=2)+'\n')
    (candidate/'DEB_SHA256.json').write_text(json.dumps(items,indent=2)+'\n')
    try:
        _run(args,log,env)
        _cache_guards(b,sysroot)
        _run(['cmake','--build',str(b),'--parallel','4'],log,env)
        e=env.copy();e['DESTDIR']=str(candidate/'install-stage')
        _run(['cmake','--install',str(b)],log,e)
        root=candidate/'install-stage/opt/neebles/modules/neebles-connect'
        binary=root/'bin/neebles-connect-desktop'
        if not binary.is_file():raise RuntimeError('No ELF installed under DESTDIR')
        if sha(root/'manifest.json')!=sha(connect/'manifest.json'):raise RuntimeError('Installed manifest digest mismatch')
        for name in ('neebles-connect-logo.png','neebles-connect-icon.png'):
            if sha(root/'assets'/name)!=sha(connect/'assets'/name):raise RuntimeError('Installed asset mismatch '+name)
        dyn=subprocess.run(['readelf','-d',str(binary)],capture_output=True,text=True,check=True)
        (candidate/'ELF_DYNAMIC.txt').write_text(dyn.stdout)
        unresolved=_elf_inventory(binary,overlay,sysroot)
        result={'status':'COMPILED_STAGED_NOT_CUSTOM_V2_CERTIFIED',
            'binary_sha256':sha(binary),'binary':str(binary),'overlay':str(overlay),
            'root':str(root),'unresolved_direct_elf_needed':unresolved,
            'note':'ELF closure is DIRECT only; transitive deps + Boss RuntimeLease still pending.'}
        (candidate/'RESULTADO.json').write_text(json.dumps(result,indent=2)+'\n')
        print('BUILD OK. DESTDIR:',root,flush=True)
        print('ELF DIRECT:',('incomplete '+', '.join(unresolved)) if unresolved else 'no direct missing in sysroot/overlay',flush=True)
        print('PENDIENTE: cierre ELF transitivo y RuntimeLease/Construction; no instalar Boss.',flush=True)
        return candidate
    except Exception as exc:
        (candidate/'FAILURE.txt').write_text(str(exc)+'\n')
        print('BLOCKED:',exc,'\nEVIDENCE:',candidate,flush=True)
        raise

def main()->int:
    p=argparse.ArgumentParser(description=__doc__)
    group=p.add_mutually_exclusive_group(required=True)
    group.add_argument('--preview',action='store_true')
    group.add_argument('--build',action='store_true')
    p.add_argument('--connect',type=Path,default=Path.home()/'NEEBLES/neebles-connect')
    p.add_argument('--custom',type=Path,default=Path.home()/'NEEBLES/neebles-custom')
    args=p.parse_args()
    try:
        print('=== CONNECT 1.0.0 / ESBIRRO F3 / NOT BOSS CERTIFIED ===',flush=True)
        connect=args.connect.expanduser().resolve();custom=args.custom.expanduser().resolve()
        items=audit(connect,custom)
        print('PRECHECK OK: ten pinned packages (seven F3 + three ELF closures); source SHA; controlled Qt/GCC',flush=True)
        if args.preview:print('PREVIEW OK: no modifications, run --build next',flush=True)
        else:build(connect,custom,items)
        return 0
    except (RuntimeError,OSError,KeyError,ValueError,subprocess.SubprocessError) as e:
        print('BLOQUEADO:',e,file=sys.stderr,flush=True)
        return 2
if __name__=='__main__':sys.exit(main())
