#!/usr/bin/env python3
"""Build Qt candidates for any NEEBLES Connect release using certified Esbirro.

The Connect version comes from manifest.json + Cargo.toml. The controlled
toolchain and immutable CUSTOM DEB/Q06/ELF pins come from an external JSON
profile. No branch or module release number is hard-coded in this engine.

--preview validates; --build stages only under ~/Downloads.
Neither mode installs Connect, launches Boss, or authorizes host Qt fallback.
Recursive ELF inspection and real Boss RuntimeLease remain distinct gates.
"""
from __future__ import annotations
import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tomllib

PROFILE_FILE=Path(__file__).with_name('connect-build-profile.json')
SOURCE_FILES:tuple[str,...]=()

def _rel(value:str)->str:
    if (not isinstance(value,str) or not value or Path(value).is_absolute()
        or '..' in Path(value).parts or '\\' in value):
        raise RuntimeError('Invalid profile relative path: '+repr(value))
    return Path(value).as_posix()

def _pin(value:str)->str:
    if not isinstance(value,str) or re.fullmatch(r'[0-9a-f]{64}',value) is None:
        raise RuntimeError('Invalid certified SHA256: '+repr(value))
    return value

def load_profile()->None:
    """Load immutable dependency policy, independent of Connect release number."""
    global DEBS,SASL,VNC_LINK_CLOSURE,QT_VERSION,GCC_MAJOR,PROFILE_SYSROOT
    global Q08B_PATCH,Q08B_PATCH_SHA,Q08B_HEADER_SOURCE_SHA,Q08B_HEADER_PATCHED_SHA
    global Q08D_DEB_SHA,Q08D_ELF_SHA,Q08D_PKG_NAME,Q08D_ORIGINAL_NAME,Q08D_ORIGINAL_SHA
    global PROXY_PKG,PROXY_VERSION,PROXY_DEB,PROXY_DEB_SHA,PROXY_RUNPATH,PROXY_ELFS

    if PROFILE_FILE.is_symlink() or not PROFILE_FILE.is_file():
        raise RuntimeError('Missing controlled build profile')
    profile=json.loads(PROFILE_FILE.read_text(encoding='utf-8'))
    if profile.get('schema')!=1 or profile.get('module')!='neebles-connect':
        raise RuntimeError('Invalid Esbirro profile schema/module')

    toolchain=profile['toolchain']
    QT_VERSION=toolchain['qt_version']
    GCC_MAJOR=toolchain['gcc_major']
    PROFILE_SYSROOT=_rel(toolchain['sysroot_relative'])
    if (re.fullmatch(r'[0-9]+(?:\\.[0-9]+){2}',QT_VERSION) is None
        or type(GCC_MAJOR) is not int or GCC_MAJOR < 10):
        raise RuntimeError('Invalid certified Qt/GCC toolchain')

    DEBS={}
    for name,item in profile['packages'].items():
        if re.fullmatch(r'[a-z0-9][a-z0-9+.-]*',name) is None:
            raise RuntimeError('Invalid package name: '+name)
        values=(item['version'],_pin(item['sha256']),_rel(item['path']),item['role'])
        if values[3] not in ('build-only','runtime-existing','runtime-candidate'):
            raise RuntimeError('Invalid package role: '+name)
        if name!='libsasl2-dev':
            DEBS[name]=values
        else:
            SASL=(name,values[0],values[1],values[2])
    if 'libsasl2-dev' not in profile['packages'] or 'libvncclient1' not in DEBS:
        raise RuntimeError('Incomplete certified package profile')

    VNC_LINK_CLOSURE=tuple(
        (p['name'],p['version'],_pin(p['sha256']),_rel(p['filename']),p['soname'])
        for p in profile['closure']
    )

    q06=profile['q06']
    Q08B_PATCH=Path(__file__).resolve().parents[2]/_rel(q06['patch_path'])
    Q08B_PATCH_SHA=_pin(q06['patch_sha256'])
    Q08B_HEADER_SOURCE_SHA=_pin(q06['header_original_sha256'])
    Q08B_HEADER_PATCHED_SHA=_pin(q06['header_patched_sha256'])
    Q08D_ELF_SHA=_pin(q06['vnc_client_elf_sha256'])
    Q08D_ORIGINAL_NAME=_rel(q06['original_deb_filename'])
    Q08D_ORIGINAL_SHA=_pin(q06['original_deb_sha256'])
    Q08D_PKG_NAME=Path(DEBS['libvncclient1'][2]).name
    Q08D_DEB_SHA=DEBS['libvncclient1'][1]

    proxy=profile['authorized_runpath']
    PROXY_PKG=proxy['package']
    PROXY_VERSION=proxy['version']
    PROXY_DEB=_rel(proxy['filename'])
    PROXY_DEB_SHA=_pin(proxy['sha256'])
    PROXY_RUNPATH=proxy['runpath']
    if (not isinstance(PROXY_RUNPATH,str)
        or not PROXY_RUNPATH.startswith('/usr/lib/')
        or '..' in Path(PROXY_RUNPATH).parts or ':' in PROXY_RUNPATH):
        raise RuntimeError('Invalid approved RUNPATH')
    PROXY_ELFS={_rel(rel):_pin(digest)
                for rel,digest in proxy['elf_sha256'].items()}
    if len(PROXY_ELFS)<2:
        raise RuntimeError('RUNPATH exception has insufficient ELF pins')


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


def release_version(connect:Path)->str:
    """Read current module version from application metadata, not script code."""
    manifest=json.loads((connect/'manifest.json').read_text(encoding='utf-8'))
    cargo=tomllib.loads((connect/'runtime/Cargo.toml').read_text(encoding='utf-8'))
    version=manifest.get('version')
    if (manifest.get('name')!='neebles-connect' or not isinstance(version,str)
        or re.fullmatch(r'[0-9]+(?:\\.[0-9]+){2}(?:[-+][0-9A-Za-z.-]+)?',version) is None
        or cargo.get('package',{}).get('version')!=version):
        raise RuntimeError('Connect manifest version and Cargo package version mismatch')
    return version


def audit(connect: Path, custom: Path) -> list[dict]:
    if not (connect / '.git').is_dir() or not (custom / '.git').is_dir():
        raise RuntimeError('Requires local Git repos Connect and CUSTOM')

    version=release_version(connect)

    # Fuente autenticada por commit + blobs Git, sin modificaciones locales.
    def git(*args: str, binary: bool = False):
        result = subprocess.run(
            ['git', '-C', str(connect), *args],
            capture_output=True, text=not binary, check=True, timeout=60
        )
        return result.stdout if binary else result.stdout.strip()

    if git('status', '--porcelain', '--untracked-files=all'):
        raise RuntimeError('Connect Git tree must be clean (including untracked files)')
    source_commit = git('rev-parse', 'HEAD')
    if len(source_commit) != 40 or any(
        ch not in '0123456789abcdef' for ch in source_commit
    ):
        raise RuntimeError('Git HEAD invalido')
    # Files are learned from Git HEAD; never add version-specific file lists.
    global SOURCE_FILES
    SOURCE_FILES=tuple(
        name.decode('utf-8')
        for name in git('ls-files','-z',binary=True).split(b'\\x00') if name
    )
    if (not SOURCE_FILES or len(SOURCE_FILES)!=len(set(SOURCE_FILES))
        or 'tooling/esbirro/build_connect.py' not in SOURCE_FILES
        or 'tooling/esbirro/connect-build-profile.json' not in SOURCE_FILES):
        raise RuntimeError('Git tracked source inventory invalid')
    for rel in SOURCE_FILES:
        path = connect / rel
        if not path.is_file() or path.is_symlink():
            raise RuntimeError('Git source missing or symlink: ' + rel)
        blob = git('show', 'HEAD:' + rel, binary=True)
        if sha(path) != hashlib.sha256(blob).hexdigest():
            raise RuntimeError('Source differs from Git blob: ' + rel)

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
    print('OK Connect',version,'Git source commit:',source_commit,flush=True)
    print('OK Lifecycle protects Secrets during update', flush=True)
    changes=subprocess.run(
        ['git','-C',str(custom),'status','--porcelain','--untracked-files=no'],
        capture_output=True,text=True,check=True,timeout=60
    ).stdout.strip()
    if changes:
        raise RuntimeError('CUSTOM V2 must be clean for controlled build')
    custom_commit=subprocess.run(
        ['git','-C',str(custom),'rev-parse','HEAD'],
        capture_output=True,text=True,check=True,timeout=60
    ).stdout.strip()
    print('CUSTOM V2 COMMIT',custom_commit,flush=True)
    tsv=custom/'runtime/manifests/modules/neebles-connect.packages.tsv'
    expected_line='\t'.join(('libvncclient1',DEBS['libvncclient1'][0],
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
    sysroot=custom/PROFILE_SYSROOT
    needed={
        'Qt6':sysroot/'usr/lib/x86_64-linux-gnu/cmake/Qt6/Qt6Config.cmake',
        'Qt6 Widgets':sysroot/('usr/lib/x86_64-linux-gnu/libQt6Widgets.so.'+QT_VERSION),
        'GCC':sysroot/('usr/bin/x86_64-linux-gnu-g++-'+str(GCC_MAJOR)),
        'GCC builtin':sysroot/('usr/lib/gcc/x86_64-linux-gnu/'+str(GCC_MAJOR)+'/include'),
        'C++ std':sysroot/('usr/include/c++/'+str(GCC_MAJOR)),
    }
    for label,path in needed.items():
        if not path.exists(): raise RuntimeError('Missing controlled sysroot '+label+': '+str(path))
        print('OK ESBIRRO',label,flush=True)
    for rel in SOURCE_FILES:
        if not (connect/rel).is_file(): raise RuntimeError('Missing Git tracked source '+rel)
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
    sysroot=custom/PROFILE_SYSROOT;b=candidate/'build'
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
