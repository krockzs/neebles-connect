#!/usr/bin/env python3
"""Verify Connect 1.0.0 F1 imported CUSTOM V2/Esbirro materials without Downloads."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

def sha256(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for chunk in iter(lambda:f.read(1<<20),b''):h.update(chunk)
    return h.hexdigest()

def field(p, f):
    r=subprocess.run(['dpkg-deb','-f',str(p),f],check=True,text=True,capture_output=True,timeout=30)
    v=r.stdout.strip()
    if not v or '\n' in v:return '<INVALID>'
    return v

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--connect',type=Path,default=Path(__file__).resolve().parents[2])
    p.add_argument('--custom',type=Path,default=Path.home()/'NEEBLES/neebles-custom')
    p.add_argument('--allow-source-changes',action='store_true',help='Report but do not fail for a deliberate later version/source edit')
    args=p.parse_args()
    connect=args.connect.expanduser();custom=args.custom.expanduser()
    pin_file=connect/'tooling/esbirro/connect-1.0.0-ssh-materials.json'
    lock_file=connect/'tooling/esbirro/m6.1-source-lock.json'
    try:
        pins=json.loads(pin_file.read_text(encoding='utf-8'))
        lock=json.loads(lock_file.read_text(encoding='utf-8'))
        errors=[]
        for entry in pins['imports']+[pins['runtime_already_present']]:
            rel=Path(entry['custom'])
            if rel.is_absolute() or '..' in rel.parts:
                raise ValueError('Invalid custom-relative package locator')
            target=custom/rel
            ok=target.is_file() and not target.is_symlink() and sha256(target)==entry['sha256']
            if ok:
                version=field(target,'Version');arch=field(target,'Architecture');pkg=field(target,'Package')
                ok=version==entry['version'] and (pkg==entry['package']) and arch=='amd64'
            print(('OK' if ok else 'FAIL'),entry['package'],target)
            if not ok:errors.append('package '+entry['package'])
        f2=json.loads((connect/'tooling/esbirro/f2-source-lock.json').read_text(encoding='utf-8'))
        for rel,expected in f2['migrated_files'].items():
            target=connect/rel
            ok=target.is_file() and not target.is_symlink() and sha256(target)==expected
            print(('OK' if ok else 'FAIL'),'migrated',rel)
            if not ok: errors.append('migrated '+rel)
        for rel,expected in lock['files'].items():
            if rel in f2['migrated_files']: continue
            target=connect/rel
            ok=target.is_file() and not target.is_symlink() and sha256(target)==expected
            if not ok:
                print(('DRIFT (allowed)' if args.allow_source_changes else 'FAIL'),rel)
                if not args.allow_source_changes:errors.append('source '+rel)
        if errors:raise RuntimeError('Integrity errors: '+', '.join(errors))
        print('VERIFICACIÓN F1 OK: material y fuente M6.1 localizables sin Downloads.')
        print('Esto NO certifica construcción, mundo ni instalación con Boss.')
        return 0
    except (OSError,KeyError,ValueError,RuntimeError,subprocess.SubprocessError,json.JSONDecodeError) as ex:
        print('BLOQUEADO:',ex,file=sys.stderr);return 2
if __name__=='__main__':sys.exit(main())
