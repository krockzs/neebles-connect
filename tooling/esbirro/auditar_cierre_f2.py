#!/usr/bin/env python3
"""Inventario y staging reproducible de seis .deb DE CUSTOM V2 para Connect 1.0.0.

No usa APT, no network, no sudo, no instala, no modifica sysroot, repos o Boss.
Stage extrae por paquete en directorios aislados bajo ~/.cache/neebles/.
NO genera mundo ni Construction, no certifica RuntimeLease.
"""
from __future__ import annotations
import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

DEBS = {
  'libvncserver-dev': ('0.9.15+dfsg-1+deb13u2','e7f41ea5630eebc9d66100aa63a185640d0120fab39b16cb63f892d7e301b43f','runtime/modules/packages/libvncserver-dev_0.9.15+dfsg-1+deb13u2_amd64.deb','build-only'),
  'libvncclient1': ('0.9.15+dfsg-1+deb13u2','13395ab10fb0cffb79e453778b22a26c1faa356fed995a6f25628238a639797a','runtime/modules/packages/libvncclient1_0.9.15+dfsg-1+deb13u2_amd64.deb','runtime-existing'),
  'libssh2-1-dev': ('1.11.1-1+deb13u2','d3267add3544b2ad8fc28b9f57c462e85448045a049725e97e4ba1e59922ea7b','portable/esbirro/materials/neebles-connect/libssh2-1-dev_1.11.1-1+deb13u2_amd64.deb','build-only'),
  'libssh2-1t64': ('1.11.1-1+deb13u2','dbb1024c192d4d292b7cfa902b96076cfe81b56a5eed4fb28da36e8bf543e49a','runtime/modules/packages/libssh2-1t64_1.11.1-1+deb13u2_amd64.deb','runtime-existing'),
  'libvterm-dev': ('0.3.3-3+b1','bc9345ef6a2e9ae9ec255588c79d563ab63dd0009b2a6b0a25a841f09e55facb','portable/esbirro/materials/neebles-connect/libvterm-dev_0.3.3-3+b1_amd64.deb','build-only'),
  'libvterm0': ('0.3.3-3+b1','91886654a1d1e157e3a43c085048ffe23a255fe25211b2e7d781b38f69a1f911','runtime/modules/packages/libvterm0_0.3.3-3+b1_amd64.deb','runtime-candidate'),
}

def sha(p:Path)->str:
    h=hashlib.sha256()
    with p.open('rb') as f:
        for block in iter(lambda:f.read(1<<20),b''):h.update(block)
    return h.hexdigest()

def check(custom:Path, emit=True)->dict:
    if not (custom/'.git').is_dir():raise RuntimeError('CUSTOM no es checkout Git')
    data=[]
    for pkg,(version,digest,relative,role) in DEBS.items():
        target=custom/relative
        if not target.is_file() or target.is_symlink() or sha(target)!=digest:
            raise RuntimeError('Material físico inválido '+pkg+': '+str(target))
        fields=[]
        for field in ('Package','Version','Architecture'):
            r=subprocess.run(['dpkg-deb','-f',str(target),field],check=True,capture_output=True,text=True,timeout=30)
            fields.append(r.stdout.strip())
        if fields != [pkg,version,'amd64']:
            raise RuntimeError('Metadatos paquete inválidos '+pkg+': '+repr(fields))
        data.append({'package':pkg,'version':version,'arch':'amd64','sha256':digest,
                     'relative_custom_path':relative,'role':role})
        if emit:print('OK',pkg,version,role,'sha='+digest[:16])
    return {'schema':1,'purpose':'Build inventory NOT a certified world or runtime','status':'STAGED_PACKAGES_NOT_CUSTOM_V2_CERTIFIED','packages':data}

def stage_packages(custom:Path, result:dict)->Path:
    # Cada DEB se extrae a un lugar aislado para evitar conflictos entre capas.
    root=Path.home()/'.cache/neebles/connect-f2-deb-overlay'
    root.mkdir(parents=True,exist_ok=True)
    stage=root/('materials_'+datetime.now().strftime('%Y%m%d_%H%M%S_%f'))
    stage.mkdir(mode=0o700)
    for item in result['packages']:
        path=custom/item['relative_custom_path']
        dest=stage/item['package']
        dest.mkdir(mode=0o700)
        r=subprocess.run(['dpkg-deb','-x',str(path),str(dest)],capture_output=True,text=True)
        if r.returncode:
            raise RuntimeError('Extracción aislada fallida '+item['package']+': '+r.stderr[:400])
        print('STAGED',item['package'],'→',dest)
    (stage/'RESULTADO.json').write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n')
    return stage

def missing_next(custom:Path):
    sysroot=custom/'build_sysroot_6.8.2'
    print('\n=== COMPILADOR CONTROLADO ===')
    for rel in ('usr/bin/x86_64-linux-gnu-g++-14','usr/lib/x86_64-linux-gnu/cmake/Qt6/Qt6Config.cmake'):
        print(('SÍ' if (sysroot/rel).exists() else 'NO'),rel)
    print('\n=== SASL HEADER + FUTURO LINK CLOSURE ===')
    print(('SÍ' if (sysroot/'usr/include/sasl/sasl.h').exists() else 'NO'),'sasl/sasl.h en sysroot')
    possible=[]
    for root in (custom/'runtime/modules/packages',custom/'portable/esbirro/materials',custom/'build_deps'):
        if root.is_dir():
            possible += list(root.glob('**/libsasl2-dev_*.deb'))[:15]
    print('Paquetes libsasl2-dev en CUSTOM:',*[str(x) for x in possible] if possible else ['NO LOCALIZADO'],sep='\n  ')
    print('ATENCIÓN: VNC incluye <sasl/sasl.h>; el cierre SASL y ELF aún requiere auditoría/receta F3.')
    print('NO se autoriza usar /usr/include ni /usr/lib del host como sustitutos.')

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    mode=ap.add_mutually_exclusive_group(required=True)
    mode.add_argument('--preview',action='store_true')
    mode.add_argument('--stage',action='store_true')
    ap.add_argument('--custom',type=Path,default=Path.home()/'NEEBLES/neebles-custom')
    args=ap.parse_args()
    custom=args.custom.expanduser().resolve()
    try:
        print('=== NEEBLES CONNECT / F2: DEBs DESDE CUSTOM SIN DOWNLOADS ===')
        inventory=check(custom)
        missing_next(custom)
        if args.preview:print('\nPREVIEW OK: seis DEB verificados, sin modificación.')
        else:
            result=stage_packages(custom,inventory)
            print('STAGE OK:',result)
            print('No compilación, no mundo certificado. No borrar Downloads aún.')
        return 0
    except (RuntimeError,OSError,subprocess.SubprocessError,ValueError) as e:
        print('BLOQUEADO:',e,file=sys.stderr);return 2
if __name__=='__main__':sys.exit(main())
