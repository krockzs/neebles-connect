#!/usr/bin/env python3
"""Connect 1.0.7: controlled Qt candidate from local Connect sources + hashed CUSTOM material.

No sudo, network, host Qt/pkg-config fallback, repository writes or Boss launch.
`--build` writes only under ~/Downloads/NEEBLES_CONNECT_107_CANDIDATOS/.
Compile/DESTDIR is only a candidate. Recursive ELF closure and RuntimeLease remain separate gates.
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

DEBS = {
  'libvncserver-dev': ('0.9.15+dfsg-1+deb13u2','e7f41ea5630eebc9d66100aa63a185640d0120fab39b16cb63f892d7e301b43f','runtime/modules/packages/libvncserver-dev_0.9.15+dfsg-1+deb13u2_amd64.deb','build-only'),
  'libvncclient1': ('0.9.15+dfsg-1+deb13u2+neebles1','57572d40da915b9e648c33f8c386f647176a146a06b633ba7a2893ce6306c9da','runtime/modules/packages/libvncclient1_0.9.15+dfsg-1+deb13u2+neebles1_amd64.deb','runtime-q08d-candidate'),
  'libssh2-1-dev': ('1.11.1-1+deb13u2','d3267add3544b2ad8fc28b9f57c462e85448045a049725e97e4ba1e59922ea7b','portable/esbirro/materials/neebles-connect/libssh2-1-dev_1.11.1-1+deb13u2_amd64.deb','build-only'),
  'libssh2-1t64': ('1.11.1-1+deb13u2','dbb1024c192d4d292b7cfa902b96076cfe81b56a5eed4fb28da36e8bf543e49a','runtime/modules/packages/libssh2-1t64_1.11.1-1+deb13u2_amd64.deb','runtime-existing'),
  'libvterm-dev': ('0.3.3-3+b1','bc9345ef6a2e9ae9ec255588c79d563ab63dd0009b2a6b0a25a841f09e55facb','portable/esbirro/materials/neebles-connect/libvterm-dev_0.3.3-3+b1_amd64.deb','build-only'),
  'libvterm0': ('0.3.3-3+b1','91886654a1d1e157e3a43c085048ffe23a255fe25211b2e7d781b38f69a1f911','runtime/modules/packages/libvterm0_0.3.3-3+b1_amd64.deb','runtime-candidate'),
}

SOURCE_FILES = ('ui/desktop/main.cpp', 'ui/desktop/MainWindow.cpp', 'ui/desktop/MainWindow.h', 'ui/desktop/VncPane.cpp', 'ui/desktop/VncPane.h', 'ui/desktop/SshPane.cpp', 'ui/desktop/SshPane.h', 'ui/desktop/SshTerminal.cpp', 'ui/desktop/SshTerminal.h', 'ui/desktop/CMakeLists.txt', 'ui/desktop/ConnectStructures.cpp', 'ui/desktop/ConnectStructures.h', 'runtime/src/main.rs', 'docs/CONNECT_Q07_RESILIENCIA_PENDIENTE.md', 'manifest.json', 'ui/desktop/ConnectionActionUi.h', 'runtime/Cargo.toml', 'runtime/Cargo.lock', 'lifecycle.json', 'README.md', 'release/notes-v1.0.7.md', 'tests/test_connect_101_contracts.py', 'tests/test_connect_107_elf_runpath.py', 'tooling/esbirro/build_connect_107.py')
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

# Q08E reuses the proven Esbirro compiler, GCC flags and Qt 6.8.2 recipe.
# It must not silently use the old Debian developer header with a new client ABI.
Q08B_PATCH = Path(__file__).resolve().parent/'patches/neebles-q06-extendeddesktopsize.patch'
Q08B_PATCH_SHA = 'd6785f5623d123cdbd88d8115fa3cab9ba9084a9fe6dcf62fb369891b7c34c50'
Q08B_HEADER_SOURCE_SHA = '422e9afa2aff6a38beb4667ea9e1ae9be4e3c421af9ab163d70b5236302a04cd'
Q08B_HEADER_PATCHED_SHA = 'aaa8c2cf639ad8bd8a61be1f7a109050a99b0a9546b6f5f093528d921b6d604c'
Q08D_DEB_SHA = '57572d40da915b9e648c33f8c386f647176a146a06b633ba7a2893ce6306c9da'
Q08D_ELF_SHA = 'dfd203cc5b291c5b61d34972102b5732a7d75108a754ae7f321c221034dbe925'
Q08D_CUSTOM_HEAD = '612a9dfa96bd287417e80c6d7d72b48c0fe96249'
Q08D_PKG_NAME = 'libvncclient1_0.9.15+dfsg-1+deb13u2+neebles1_amd64.deb'
Q08D_ORIGINAL_NAME = 'libvncclient1_0.9.15+dfsg-1+deb13u2_amd64.deb'
Q08D_ORIGINAL_SHA = '13395ab10fb0cffb79e453778b22a26c1faa356fed995a6f25628238a639797a'

# C64E: material autenticado de CUSTOM V2, verificado fisicamente contra
# libproxy1v5_0.5.9-1_amd64.deb. No se concede fallback al anfitrion.
PROXY_DEB = 'libproxy1v5_0.5.9-1_amd64.deb'
PROXY_DEB_SHA = '794a8526989464cb17f1dec9dab921f0e5afb82bcc665e938fac925be47698eb'
PROXY_RUNPATH = '/usr/lib/x86_64-linux-gnu/libproxy'
PROXY_ELFS = {
    'usr/lib/x86_64-linux-gnu/libproxy.so.0.5.9':
        '88a785edfa73af6727fcea089e008cbfbb253cd77c2532d7577e8f2c4cafc805',
    'usr/lib/x86_64-linux-gnu/libproxy/libpxbackend-1.0.so':
        'ca5c681cb332fb373975f2fff8e47ba65323783a3ae2c4f8aa4d152720f140fd',
}

# Sources from the actual Q07E incremental tree, reconstructed and re-hashed.
# C64: identificar fuentes contra el commit Git seleccionado, no por
# hashes incrustados de un build anterior.
SOURCE_BRANCH = 'feature/connect-1.0.7-c63-vnc-fill-logo'

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

def validate_q06_sources()->None:
    # Paquete de desarrollo autenticado por CUSTOM.
    # No se usan fuentes temporales Q08B en Downloads.
    if not Q08B_PATCH.is_file() or Q08B_PATCH.is_symlink():
        raise RuntimeError('Q06 patch missing or symlink')

    if sha(Q08B_PATCH) != Q08B_PATCH_SHA:
        raise RuntimeError('Q06 patch digest mismatch')


def stage_q06_header(overlay:Path,candidate:Path)->None:
    source = overlay/'usr/include/rfb/rfbclient.h'

    if not source.is_file() or source.is_symlink():
        raise RuntimeError('Q06 developer header not found')

    if sha(source) != Q08B_HEADER_SOURCE_SHA:
        raise RuntimeError('Q06 developer header identity mismatch')

    sandbox = candidate/'q06-header-source'
    target = sandbox/'include/rfb/rfbclient.h'
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)

    cmd = [
        'git', 'apply',
        '--include=include/rfb/rfbclient.h',
        str(Q08B_PATCH)
    ]

    verify = subprocess.run(
        cmd[:2] + ['--check'] + cmd[2:],
        cwd=sandbox,
        capture_output=True,
        text=True
    )

    if verify.returncode:
        raise RuntimeError(
            'Q06 header patch invalid: '
            + verify.stderr[-500:]
        )

    subprocess.run(cmd, cwd=sandbox, check=True)

    if sha(target) != Q08B_HEADER_PATCHED_SHA:
        raise RuntimeError(
            'Q06 patched header does not match certified hash'
        )

    shutil.copy2(target, source)

    if sha(source) != Q08B_HEADER_PATCHED_SHA:
        raise RuntimeError(
            'Q06 overlay header not installed correctly'
        )

    print(
        'Q06 HEADER OK: authenticated CUSTOM DEB '
        '+ versioned Q06 patch',
        flush=True
    )


def audit(connect: Path, custom: Path) -> list[dict]:
    if not (connect / '.git').is_dir() or not (custom / '.git').is_dir():
        raise RuntimeError('Requires local Git repos Connect and CUSTOM')

    manifest = json.loads(
        (connect / 'manifest.json').read_text(encoding='utf-8')
    )

    if manifest.get('version') != '1.0.7':
        raise RuntimeError('Connect release candidate must declare 1.0.7')

    # Fuente autenticada por commit + blobs Git, sin modificaciones locales.
    def git(*args: str, binary: bool = False):
        result = subprocess.run(
            ['git', '-C', str(connect), *args],
            capture_output=True, text=not binary, check=True, timeout=60
        )
        return result.stdout if binary else result.stdout.strip()

    if git('branch', '--show-current') != SOURCE_BRANCH:
        raise RuntimeError('C64 requiere rama candidata: ' + SOURCE_BRANCH)
    if git('status', '--porcelain', '--untracked-files=no'):
        raise RuntimeError('Connect tiene cambios locales versionados')
    source_commit = git('rev-parse', 'HEAD')
    if len(source_commit) != 40 or any(
        ch not in '0123456789abcdef' for ch in source_commit
    ):
        raise RuntimeError('Git HEAD invalido')
    if len(SOURCE_FILES) != len(set(SOURCE_FILES)):
        raise RuntimeError('Inventario de fuentes duplicado')
    for rel in SOURCE_FILES:
        path = connect / rel
        if not path.is_file() or path.is_symlink():
            raise RuntimeError('C64 source missing or symlink: ' + rel)
        blob = git('show', 'HEAD:' + rel, binary=True)
        if sha(path) != hashlib.sha256(blob).hexdigest():
            raise RuntimeError('C64 source differs from Git blob: ' + rel)

    lifecycle = json.loads(
        (connect / 'lifecycle.json').read_text(encoding='utf-8')
    )
    if lifecycle['transitions']['update']['operations'] != {}:
        raise RuntimeError('Update no debe ejecutar install-vault')
    if lifecycle['transitions']['install']['operations'][
        'ensure-secrets-vault']['munition']['step'] != 'install-vault':
        raise RuntimeError('Lifecycle install contract altered')
    if lifecycle['transitions']['open']['operations'][
        'launch-runtime']['munition']['step'] != 'open-runtime':
        raise RuntimeError('Lifecycle open contract altered')
    print('OK Connect 1.0.7 Git source commit:', source_commit, flush=True)
    print('OK Lifecycle protects Secrets during update', flush=True)
    changes=subprocess.run(
        ['git','-C',str(custom),'status','--porcelain','--untracked-files=no'],
        capture_output=True,text=True,check=True,timeout=60
    ).stdout.strip()
    if changes:
        raise RuntimeError('CUSTOM V2 debe estar limpio para C64')
    custom_commit=subprocess.run(
        ['git','-C',str(custom),'rev-parse','HEAD'],
        capture_output=True,text=True,check=True,timeout=60
    ).stdout.strip()
    print('CUSTOM V2 COMMIT',custom_commit,flush=True)
    tsv=custom/'runtime/manifests/modules/neebles-connect.packages.tsv'
    expected_line='\t'.join(('libvncclient1','0.9.15+dfsg-1+deb13u2+neebles1',
                              'amd64',Q08D_PKG_NAME,Q08D_DEB_SHA))
    lines=tsv.read_text(encoding='utf-8').splitlines()
    if lines.count(expected_line)!=1 or sum(x.startswith('libvncclient1\t') for x in lines)!=1:
        raise RuntimeError('CUSTOM Q08D membership is not the exact candidate')
    manifest=json.loads((custom/'runtime/manifests/modules/neebles-connect.manifest.json').read_text(encoding='utf-8'))
    entry=[x for x in manifest.get('entries',[]) if x.get('path')=='packages/'+Q08D_PKG_NAME]
    if (manifest.get('module')!='neebles-connect' or len(entry)!=1
        or entry[0].get('sha256')!=Q08D_DEB_SHA
        or entry[0].get('size')!=(custom/'runtime/modules/packages'/Q08D_PKG_NAME).stat().st_size):
        raise RuntimeError('CUSTOM Q08D integrity manifest differs')
    if sha(custom/'runtime/modules/packages'/Q08D_ORIGINAL_NAME)!=Q08D_ORIGINAL_SHA:
        raise RuntimeError('Original Debian libvncclient DEB changed')
    validate_q06_sources()
    items=[]
    for name,(version,digest,relative,role) in DEBS.items():
        check_deb(custom/relative,name,version,digest)
        items.append(dict(package=name,version=version,sha256=digest,path=str(custom/relative),role=role))
        print('OK MATERIAL',name,version,flush=True)
    name,version,digest,rel=SASL
    check_deb(custom/rel,name,version,digest)
    items.append(dict(package=name,version=version,sha256=digest,path=str(custom/rel),role='build-only'))
    check_custom_membership(custom/'runtime/manifests/modules/neebles-connect.packages.tsv')
    for name,version,digest,filename,soname in VNC_LINK_CLOSURE:
        material=custom/'runtime/modules/packages'/filename
        check_deb(material,name,version,digest)
        items.append(dict(package=name,version=version,sha256=digest,path=str(material),role='runtime-existing'))
        print('OK ELF MATERIAL',name,soname,flush=True)
    sysroot=custom/'build_sysroot_6.8.2'
    needed={
        'Qt6':sysroot/'usr/lib/x86_64-linux-gnu/cmake/Qt6/Qt6Config.cmake',
        'Qt6 Widgets':sysroot/'usr/lib/x86_64-linux-gnu/libQt6Widgets.so.6.8.2',
        'GCC14':sysroot/'usr/bin/x86_64-linux-gnu-g++-14',
        'GCC builtin':sysroot/'usr/lib/gcc/x86_64-linux-gnu/14/include',
        'C++ std':sysroot/'usr/include/c++/14',
    }
    for label,path in needed.items():
        if not path.exists(): raise RuntimeError('Missing controlled sysroot '+label+': '+str(path))
        print('OK ESBIRRO',label,flush=True)
    for rel in SOURCE_FILES:
        if not (connect/rel).is_file(): raise RuntimeError('Missing Connect Q08E source '+rel)
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
    lib_elf=overlay/'usr/lib/x86_64-linux-gnu/libvncclient.so.0.9.15'
    if not lib_elf.is_file() or lib_elf.is_symlink() or sha(lib_elf)!=Q08D_ELF_SHA:
        raise RuntimeError('Q08D ELF candidate differs from Q08C certified source evidence')
    stage_q06_header(overlay,candidate)
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

def _certify_proxy_material(sysroot:Path)->None:
    # Solo se acepta el paquete libproxy1v5 ya declarado por CUSTOM V2.
    # SHA del DEB, membresia y manifiesto se verifican ANTES del recorrido.
    custom=sysroot.resolve(strict=True).parent
    package=custom/'runtime/modules/packages'/PROXY_DEB
    check_deb(package,'libproxy1v5','0.5.9-1',PROXY_DEB_SHA)
    tsv=custom/'runtime/manifests/modules/neebles-connect.packages.tsv'
    expected='\t'.join(('libproxy1v5','0.5.9-1','amd64',PROXY_DEB,PROXY_DEB_SHA))
    if tsv.read_text(encoding='utf-8').splitlines().count(expected)!=1:
        raise RuntimeError('CUSTOM V2 libproxy membership mismatch')
    manifest=json.loads((custom/'runtime/manifests/modules/neebles-connect.manifest.json').read_text(encoding='utf-8'))
    matches=[e for e in manifest.get('entries',[]) if e.get('path')=='packages/'+PROXY_DEB]
    if (manifest.get('module')!='neebles-connect' or len(matches)!=1
        or matches[0].get('sha256')!=PROXY_DEB_SHA
        or matches[0].get('size')!=package.stat().st_size):
        raise RuntimeError('CUSTOM V2 libproxy manifest mismatch')
    for rel,digest in PROXY_ELFS.items():
        physical=sysroot/rel
        if not physical.is_file() or not physical.resolve(strict=True).is_relative_to(sysroot.resolve()):
            raise RuntimeError('Missing or escaped Esbirro libproxy ELF: '+rel)
        if sha(physical)!=digest:
            raise RuntimeError('Esbirro libproxy ELF identity mismatch: '+rel)
    print('C64E PROXY DEB/MANIFEST/ELF: exact authenticated identities OK',flush=True)


def _needed(binary:Path,sysroot:Path|None=None)->tuple[list[str],list[Path]]:
    r=subprocess.run(['readelf','-d',str(binary)],check=True,capture_output=True,text=True)
    needed=[]
    runpath=[]
    for line in r.stdout.splitlines():
        if '(NEEDED)' in line:
            if '[' not in line or ']' not in line:
                raise RuntimeError('Malformed ELF NEEDED: '+str(binary))
            needed.append(line.split('[',1)[1].split(']',1)[0])
        if '(RPATH)' in line:
            raise RuntimeError('DT_RPATH not authorized: '+str(binary))
        if '(RUNPATH)' in line:
            if '[' not in line or ']' not in line:
                raise RuntimeError('Malformed ELF RUNPATH: '+str(binary))
            runpath.append(line.split('[',1)[1].split(']',1)[0])
    if not runpath:
        return needed,[]
    if sysroot is None or runpath!=[PROXY_RUNPATH]:
        raise RuntimeError('Unapproved ELF RUNPATH: '+str(binary)+' '+repr(runpath))
    real=binary.resolve(strict=True)
    root=sysroot.resolve(strict=True)
    if not real.is_relative_to(root):
        raise RuntimeError('RUNPATH ELF escapes controlled sysroot: '+str(binary))
    relative=real.relative_to(root).as_posix()
    digest=PROXY_ELFS.get(relative)
    if digest is None or sha(real)!=digest:
        raise RuntimeError('RUNPATH ELF identity not certified: '+relative)
    # El RUNPATH absoluto es una ruta del mundo destino, JAMAS del host.
    # Solo proyectamos su ruta homologada dentro del sysroot autenticado.
    return needed,[sysroot/'usr/lib/x86_64-linux-gnu/libproxy']


def _recursive_elf_audit(binary:Path,overlay:Path,sysroot:Path,candidate:Path)->dict:
    # Inspeccion fisica; no prueba por si sola la resolucion del cargador
    # real dentro del RuntimeLease. Nunca consultar rutas del host.
    _certify_proxy_material(sysroot)
    roots=[overlay/'usr/lib/x86_64-linux-gnu',overlay/'lib/x86_64-linux-gnu',
           overlay/'usr/lib',sysroot/'usr/lib/x86_64-linux-gnu',
           sysroot/'lib/x86_64-linux-gnu',sysroot/'usr/lib',sysroot/'lib']
    work=[binary]
    seen=set()
    missing=set()
    records=[]
    accepted_runpaths=[]
    while work:
        current=work.pop(0)
        real=current.resolve(strict=True)
        if real in seen:continue
        if len(seen)>800:raise RuntimeError('Recursive ELF closure unexpectedly exceeds 800 objects')
        if not (real.is_relative_to(candidate.resolve()) or real.is_relative_to(sysroot.resolve())):
            raise RuntimeError('ELF escapes Esbirro controlled roots: '+str(real))
        seen.add(real)
        needed,runpath_roots=_needed(current,sysroot)
        if runpath_roots:
            accepted_runpaths.append(str(real))
        targets={}
        for soname in needed:
            if not soname or '/' in soname or soname in ('.','..'):
                raise RuntimeError('Invalid ELF dependency name: '+soname)
            # DT_RUNPATH solo afecta dependencias directas de ESTE ELF.
            # Se busca exclusivamente en rutas homologadas del mundo.
            choices=[p/soname for p in [*runpath_roots,*roots] if (p/soname).is_file()]
            if not choices:
                missing.add(soname)
                targets[soname]=None
                continue
            selected=choices[0]
            resolved=selected.resolve(strict=True)
            if not(resolved.is_relative_to(overlay.resolve()) or resolved.is_relative_to(sysroot.resolve())):
                raise RuntimeError('ELF dependency escapes controlled world: '+str(selected))
            targets[soname]=str(selected)
            work.append(selected)
        records.append({'file':str(current),'needed':needed,
                        'allowed_runpath':PROXY_RUNPATH if runpath_roots else None,
                        'resolved':targets})
    output={'status':'ELF_CLOSURE_CANDIDATE_NOT_RUNTIME_CERTIFICATION',
            'root_elf':str(binary),'visited_elf_files':len(seen),
            'missing':sorted(missing),
            'accepted_certified_runpaths':accepted_runpaths,
            'records':records}
    (candidate/'ELF_CLOSURE.json').write_text(json.dumps(output,indent=2)+'\n',encoding='utf-8')
    print('ELF RECURSIVE:',len(seen),'objects;',len(missing),'missing;',
          len(accepted_runpaths),'certified RUNPATHs',flush=True)
    return output

def _elf_inventory(binary:Path,overlay:Path,sysroot:Path)->list[str]:
    search=[overlay/'usr/lib/x86_64-linux-gnu', sysroot/'usr/lib/x86_64-linux-gnu',
        sysroot/'lib/x86_64-linux-gnu',sysroot/'usr/lib',sysroot/'lib']
    missing=[]
    for item in _needed(binary)[0]:
        p=next((d/item for d in search if (d/item).is_file()),None)
        if p is None:missing.append(item)
        print(('FOUND' if p else 'NOT FOUND'),'ELF NEEDED',item,flush=True)
    return missing

def build(connect:Path,custom:Path,items:list[dict])->Path:
    for command in ('cmake','readelf','dpkg-deb'):
        if shutil.which(command) is None:raise RuntimeError('Missing utility '+command)
    stage_base=Path.home()/'Downloads/NEEBLES_CONNECT_107_CANDIDATOS'
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
        f'-DNEEBLES_CONNECT_DEP_OVERLAY={overlay}',
        '-DCMAKE_REQUIRED_LINK_OPTIONS='+(';'.join(['-Wl,-rpath-link,'+str(overlay/'usr/lib/x86_64-linux-gnu'),
                                                '-Wl,-rpath-link,'+str(sysroot/'usr/lib/x86_64-linux-gnu'),
                                                '-Wl,-rpath-link,'+str(sysroot/'lib/x86_64-linux-gnu')]))]
    (candidate/'SOURCE_SHA256.json').write_text(json.dumps({r:sha(connect/r) for r in SOURCE_FILES},indent=2)+'\n')
    source_commit=subprocess.run(['git','-C',str(connect),'rev-parse','HEAD'],capture_output=True,text=True,check=True).stdout.strip()
    custom_commit=subprocess.run(['git','-C',str(custom),'rev-parse','HEAD'],capture_output=True,text=True,check=True).stdout.strip()
    (candidate/'SOURCE_COMMIT.txt').write_text(source_commit+'\n')
    (candidate/'CUSTOM_COMMIT.txt').write_text(custom_commit+'\n')
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
        if unresolved:
            raise RuntimeError('Direct ELF DT_NEEDED unresolved: '+', '.join(unresolved))
        # Recursive physical ELF certification is a SEPARATE gate (Q08E-R4R2).
        # Do not confuse a successful compiler/install-stage with that gate.
        result={'status':'CONNECT_107_QT_STAGED_PENDING_ELF_RUNTIME_QA',
            'binary_sha256':sha(binary),'binary':str(binary),'overlay':str(overlay),
            'root':str(root),'unresolved_direct_elf_needed':unresolved,
            'recursive_elf_status':'NOT_EXECUTED_SEPARATE_Q08E_R4R2_GATE',
            'runtimelease_status':'NOT_TESTED',
            'note':'Compiler+DESTDIR+direct ELF only. Q08E-R4R2 physical closure and Boss RuntimeLease real acceptance must be repeated for this binary.'}
        (candidate/'RESULTADO.json').write_text(json.dumps(result,indent=2)+'\n')
        print('BUILD OK. DESTDIR:',root,flush=True)
        print('ELF DIRECT:',('incomplete '+', '.join(unresolved)) if unresolved else 'no direct missing in sysroot/overlay',flush=True)
        print('PENDIENTE: Q08E-R4R2 cierre ELF fisico y Boss RuntimeLease; no instalar.',flush=True)
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
        print('=== CONNECT 1.0.7 / ESBIRRO QT / NO INSTALADO ===',flush=True)
        connect=args.connect.expanduser().resolve();custom=args.custom.expanduser().resolve()
        items=audit(connect,custom)
        print('PRECHECK OK: C64 sources + Q08D DEB + Q06 header + controlled Qt 6.8.2/GCC14',flush=True)
        if args.preview:print('PREVIEW OK: no modifications, run --build next',flush=True)
        else:build(connect,custom,items)
        return 0
    except (RuntimeError,OSError,KeyError,ValueError,subprocess.SubprocessError) as e:
        print('BLOQUEADO:',e,file=sys.stderr,flush=True)
        return 2
if __name__=='__main__':sys.exit(main())
