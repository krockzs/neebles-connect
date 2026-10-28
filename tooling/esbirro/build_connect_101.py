#!/usr/bin/env python3
"""Connect Q08E: controlled Qt candidate from local Connect sources + hashed CUSTOM material.

No sudo, network, host Qt/pkg-config fallback, repository writes or Boss launch.
`--build` writes only under ~/Downloads/NEEBLES_CONNECT_Q08E_CANDIDATOS/.
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

SOURCE_FILES = ('ui/desktop/main.cpp','ui/desktop/MainWindow.cpp','ui/desktop/MainWindow.h',
    'ui/desktop/VncPane.cpp','ui/desktop/VncPane.h','ui/desktop/SshPane.cpp',
    'ui/desktop/SshPane.h','ui/desktop/SshTerminal.cpp','ui/desktop/SshTerminal.h',
    'ui/desktop/CMakeLists.txt','ui/desktop/ConnectStructures.cpp','ui/desktop/ConnectStructures.h', 'runtime/src/main.rs','docs/CONNECT_Q07_RESILIENCIA_PENDIENTE.md', 'manifest.json')
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
Q08B_STAGE = Path.home()/'Downloads/NEEBLES_CONNECT_Q08B_FUENTE_DEBIAN'
Q08B_SOURCE = Q08B_STAGE/'libvncserver-0.9.15+dfsg'
Q08B_PATCH = Path(__file__).resolve().parent/'patches/neebles-q06-extendeddesktopsize.patch'
Q08B_PATCH_SHA = 'd6785f5623d123cdbd88d8115fa3cab9ba9084a9fe6dcf62fb369891b7c34c50'
Q08B_HEADER_SOURCE_SHA = '422e9afa2aff6a38beb4667ea9e1ae9be4e3c421af9ab163d70b5236302a04cd'
Q08B_HEADER_PATCHED_SHA = 'aaa8c2cf639ad8bd8a61be1f7a109050a99b0a9546b6f5f093528d921b6d604c'
Q08B_C_SOURCE_SHA = '7010aba9a90ab9eebf3cffa54125c28b152099cdf2bbf6a59a89bf3271377a42'
Q08B_C_PATCHED_SHA = 'eb008639b90a79e63d9e5b805ac4a30951ab49c0a9671eac41cb24946acfddc6'
Q08D_DEB_SHA = '57572d40da915b9e648c33f8c386f647176a146a06b633ba7a2893ce6306c9da'
Q08D_ELF_SHA = 'dfd203cc5b291c5b61d34972102b5732a7d75108a754ae7f321c221034dbe925'
Q08D_CUSTOM_HEAD = '612a9dfa96bd287417e80c6d7d72b48c0fe96249'
Q08D_PKG_NAME = 'libvncclient1_0.9.15+dfsg-1+deb13u2+neebles1_amd64.deb'
Q08D_ORIGINAL_NAME = 'libvncclient1_0.9.15+dfsg-1+deb13u2_amd64.deb'
Q08D_ORIGINAL_SHA = '13395ab10fb0cffb79e453778b22a26c1faa356fed995a6f25628238a639797a'

# Sources from the actual Q07E incremental tree, reconstructed and re-hashed.
EXPECTED_SOURCE_SHA256 = {
    'runtime/src/main.rs':'ab0ffc6268ba6b60db3732adf723c675ac39bd71e3d56fc82087c0a54549bb24',
    'ui/desktop/main.cpp':'44476eaf6f24075e5125501fb94cc2a2d21d1c765acdb78de01043b737d27977',
    'ui/desktop/CMakeLists.txt':'ee4729ee63a103f375f4fb497390e2d0fc19d61cd01a212e1463e54750ba09c4',
    'ui/desktop/MainWindow.cpp':'7274b34c407879d4d39a22320fd3e3d48f26f923738c1890c584c4eae793b281',
    'ui/desktop/MainWindow.h':'0b0479e969c802250fd3509997ffc4f8ef04041adb4cb94539b7d79af13e064a',
    'ui/desktop/VncPane.cpp':'48dc4892de6174c6d20e3dbcd225707dd27322f394fde5c56b5bdcc4646d14d7',
    'ui/desktop/VncPane.h':'6da6fd6fd581bb43a6746471884349c540ce08aebc70f88ba52e66367151e209',
    'ui/desktop/SshPane.cpp':'4ece7445a79da95efe9815f5bfc37b08aafb32882a1811db74161d54398db083',
    'ui/desktop/SshPane.h':'2de7d024dd7809483d2d07074b58dd66c80bdc6a591d030ffecb39a8749d5de8',
    'ui/desktop/ConnectStructures.cpp':'0e6aebb49524ed607f116cb15f63e7f9a5262c36fb137c405f1767eb6e84a001',
    'ui/desktop/ConnectStructures.h':'56b446bdad35805b9c0d6b69dda7a5a4a5e549cac4e36f195e2d5de002470c7f',
    'ui/desktop/SshTerminal.cpp':'4b2073554bc6bead22e8af6d0458f90deddd872f5d533b099d28677bb61c8e05',
    'ui/desktop/SshTerminal.h':'e561e4762b252f4d1ac4234d102b9ae300f8ab51858c505208e437bfb39cb7ea',
    'docs/CONNECT_Q07_RESILIENCIA_PENDIENTE.md':'51510a5e39e008bac0884c461214cd8e3b26edf895f4ceffe38531009d4b00a5',
}

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
    if sha(Q08B_PATCH)!=Q08B_PATCH_SHA:
        raise RuntimeError('Q08B topology patch SHA does not match the verified source')
    series=(Q08B_SOURCE/'debian/patches/series').read_text().splitlines()
    expected=['0001_CVE-2026-32853.patch','0002_CVE-2026-32854.patch',
              '0003_CVE-2026-44988.patch','0004_CVE-2026-50538.patch']
    if [line.strip() for line in series if line.strip() and not line.startswith('#')]!=expected:
        raise RuntimeError('Q08B security patch series has changed')
    for rel, digest in [('include/rfb/rfbclient.h',Q08B_HEADER_SOURCE_SHA),
                        ('src/libvncclient/rfbclient.c',Q08B_C_SOURCE_SHA)]:
        if sha(Q08B_SOURCE/rel)!=digest:
            raise RuntimeError('Q08B Debian source identity changed: '+rel)

def stage_q06_header(overlay:Path,candidate:Path)->None:
    # Apply source patch only on a disposable copy, never in CUSTOM or Q08B.
    sandbox=candidate/'q06-header-source'
    for rel in ('include/rfb/rfbclient.h','src/libvncclient/rfbclient.c'):
        output=sandbox/rel
        output.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(Q08B_SOURCE/rel,output)
    r=subprocess.run(['git','apply','--check',str(Q08B_PATCH)],cwd=sandbox,
                     capture_output=True,text=True)
    if r.returncode:
        raise RuntimeError('Q06 patch cannot apply to verified Debian sources: '+r.stderr[:350])
    subprocess.run(['git','apply',str(Q08B_PATCH)],cwd=sandbox,check=True)
    if sha(sandbox/'include/rfb/rfbclient.h')!=Q08B_HEADER_PATCHED_SHA or \
       sha(sandbox/'src/libvncclient/rfbclient.c')!=Q08B_C_PATCHED_SHA:
        raise RuntimeError('Q06 patched source hashes differ from Q08B report')
    # Verify the physical DEB developer header is same Debian header before replacing.
    target=overlay/'usr/include/rfb/rfbclient.h'
    if sha(target)!=Q08B_HEADER_SOURCE_SHA:
        raise RuntimeError('LibVNC developer DEB header differs from authenticated Debian source')
    shutil.copy2(sandbox/'include/rfb/rfbclient.h',target)
    if sha(target)!=Q08B_HEADER_PATCHED_SHA:
        raise RuntimeError('Q06 header staging failed')
    print('Q06 HEADER OK: verified Debian source + topology patch, overlay only',flush=True)

def audit(connect: Path, custom: Path) -> list[dict]:
    if not (connect/'.git').is_dir() or not (custom/'.git').is_dir():
        raise RuntimeError('Requires local Git repos Connect and CUSTOM')
    if json.loads((connect/'manifest.json').read_text())['version'] != '1.0.1':
        raise RuntimeError('Connect version is not 1.0.1')
    # Qt identity is pinned to Q08E-R2. Rust was subsequently formatted
    # under Q08F-R1; its identity comes from the Q08G source/ELF attestation.
    attestation_path = connect/'tooling/esbirro/connect-q08g-release-candidate.json'
    if attestation_path.is_symlink() or not attestation_path.is_file():
        raise RuntimeError('Missing Q08G signed-off candidate provenance')
    attestation = json.loads(attestation_path.read_text(encoding='utf-8'))
    if (attestation.get('state')!='RELEASE_CANDIDATE_NOT_CERTIFIED'
        or attestation.get('published') is not False
        or attestation.get('runtimelease_accepted') is not False
        or attestation.get('module')!='neebles-connect'
        or attestation.get('version')!='1.0.1'
        or attestation.get('qt',{}).get('sha256')!='416ecbce17ecb2bf7db438c93912b4cbe6be950d3a850cbc22323369fdb3ac42'
        or attestation.get('rust',{}).get('sha256')!='b9154c0690bd581917a0457a671aafc43a3561c173d906b9f4e10c6e4eb24405'
        or attestation.get('libvncclient',{}).get('deb_sha256')!=Q08D_DEB_SHA):
        raise RuntimeError('Q08G candidate attestation identity inconsistent')
    rust_source_sha=attestation.get('rust',{}).get('source_sha256')
    if not isinstance(rust_source_sha,str) or len(rust_source_sha)!=64 or any(x not in '0123456789abcdef' for x in rust_source_sha):
        raise RuntimeError('Q08G Rust source SHA256 missing or invalid')
    expected_sources=EXPECTED_SOURCE_SHA256.copy()
    expected_sources['runtime/src/main.rs']=rust_source_sha
    for rel, expected in expected_sources.items():
        file = connect/rel
        if not file.is_file() or file.is_symlink() or sha(file) != expected:
            raise RuntimeError('Connect Q07E source identity mismatch: '+rel)
    changes=subprocess.run(['git','-C',str(custom),'diff','--name-only'],capture_output=True,text=True,check=True).stdout.splitlines()
    expected_changes={'runtime/manifests/modules/neebles-connect.packages.tsv',
                      'runtime/manifests/modules/neebles-connect.manifest.json'}
    if set(changes) not in (set(), expected_changes):
        raise RuntimeError('CUSTOM tracked changes differ from Q08D candidate or clean committed state: '+repr(changes))
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

def _needed(binary:Path)->list[str]:
    r=subprocess.run(['readelf','-d',str(binary)],check=True,capture_output=True,text=True)
    needed=[]
    for line in r.stdout.splitlines():
        if '(NEEDED)' in line and '[' in line:needed.append(line.split('[',1)[1].split(']',1)[0])
        if '(RPATH)' in line or '(RUNPATH)' in line:raise RuntimeError('RPATH/RUNPATH not allowed in ELF')
    return needed

def _recursive_elf_audit(binary:Path,overlay:Path,sysroot:Path,candidate:Path)->dict:
    # Purely observational recursive ELF scan of the controlled candidate world.
    roots=[overlay/'usr/lib/x86_64-linux-gnu',overlay/'lib/x86_64-linux-gnu',
           overlay/'usr/lib',sysroot/'usr/lib/x86_64-linux-gnu',
           sysroot/'lib/x86_64-linux-gnu',sysroot/'usr/lib',sysroot/'lib']
    work=[binary]
    seen=set()
    missing=set()
    records=[]
    while work:
        current=work.pop(0)
        real=current.resolve(strict=True)
        if real in seen:continue
        if len(seen)>800:raise RuntimeError('Recursive ELF closure unexpectedly exceeds 800 objects')
        if not (real.is_relative_to(candidate.resolve()) or real.is_relative_to(sysroot.resolve())):
            raise RuntimeError('ELF escapes Esbirro controlled roots: '+str(real))
        seen.add(real)
        needed=_needed(current)
        targets={}
        for soname in needed:
            if '/' in soname or soname in ('.','..'):
                raise RuntimeError('Invalid ELF dependency name: '+soname)
            choices=[p/soname for p in roots if (p/soname).is_file()]
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
        records.append({'file':str(current),'needed':needed,'resolved':targets})
    output={'status':'ELF_CLOSURE_CANDIDATE_NOT_RUNTIME_CERTIFICATION',
            'root_elf':str(binary),'visited_elf_files':len(seen),
            'missing':sorted(missing),'records':records}
    (candidate/'ELF_CLOSURE.json').write_text(json.dumps(output,indent=2)+'\n',encoding='utf-8')
    print('ELF RECURSIVE:',len(seen),'objects;',len(missing),'missing',flush=True)
    return output

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
    stage_base=Path.home()/'Downloads/NEEBLES_CONNECT_Q08E_CANDIDATOS'
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
        result={'status':'Q08I_QT_COMPILED_STAGED_PENDING_PHYSICAL_ELF_RUNTIME_QA',
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
        print('=== CONNECT Q08E / ESBIRRO QT / STAGED, NOT INSTALLED ===',flush=True)
        connect=args.connect.expanduser().resolve();custom=args.custom.expanduser().resolve()
        items=audit(connect,custom)
        print('PRECHECK OK: Q07E sources + Q08D DEB + Q06 header + controlled Qt 6.8.2/GCC14',flush=True)
        if args.preview:print('PREVIEW OK: no modifications, run --build next',flush=True)
        else:build(connect,custom,items)
        return 0
    except (RuntimeError,OSError,KeyError,ValueError,subprocess.SubprocessError) as e:
        print('BLOQUEADO:',e,file=sys.stderr,flush=True)
        return 2
if __name__=='__main__':sys.exit(main())
