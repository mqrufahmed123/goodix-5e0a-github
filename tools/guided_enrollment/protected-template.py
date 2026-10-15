#!/usr/bin/python3
"""Back up/restore one selected enrollment. Template bytes stay root-only."""
from pathlib import Path
import hashlib
import json
import os
import pwd
import re
import shutil
import subprocess
import sys

def main():
    if os.geteuid() != 0 or 'PKEXEC_UID' not in os.environ:
        raise SystemExit('Run through pkexec from your normal user session.')
    if len(sys.argv) not in (4,5):
        raise SystemExit('Usage: protected-template.py backup|stage|keep|restore SESSION FINGER [PRIOR_BASELINE_SESSION]')
    action, session, finger = sys.argv[1:4]
    prior_session=sys.argv[4] if len(sys.argv)==5 else None
    if action not in ('backup', 'stage', 'keep', 'restore') or not re.fullmatch(r'[0-9]{8}T[0-9]{6}-[a-f0-9]{8}', session):
        raise SystemExit('Invalid operation or session identifier.')
    if prior_session and (action!='backup' or not re.fullmatch(r'[0-9]{8}T[0-9]{6}-[a-f0-9]{8}',prior_session)):
        raise SystemExit('Invalid baseline session.')
    numbers = {'right-index-finger':7, 'right-thumb':6}
    if finger not in numbers:
        raise SystemExit('Unsupported finger.')
    uid = int(os.environ['PKEXEC_UID'])
    user = pwd.getpwuid(uid).pw_name
    original = Path('/var/lib/fprint')/user/'goodixtls5e0a/0'/str(numbers[finger])
    directory = Path('/var/backups/goodix-full-pad')/session
    os.umask(0o077)
    if action == 'backup':
        if directory.exists() or original.is_symlink() or not original.is_file():
            raise SystemExit('A new session and an existing selected enrollment are required.')
        directory.mkdir(parents=True,mode=0o700)
        shutil.copyfile(original,directory/'original-template')
        digest=hashlib.sha256((directory/'original-template').read_bytes()).hexdigest()
        if prior_session:
            prior=json.loads((directory.parent/prior_session/'metadata.json').read_text())
            if (prior['uid']!=uid or prior['finger']!=finger or prior['sha256']!=digest
                    or prior.get('state')!='restored-original'):
                raise SystemExit('Current enrollment does not match the restored baseline; start a fresh comparison.')
        (directory/'metadata.json').write_text(json.dumps({'uid':uid,'finger':finger,'sha256':digest,'state':'backed-up'}))
        print('Selected enrollment backed up in the protected system directory.',flush=True)
        return
    if directory.is_symlink():
        raise SystemExit('Unexpected backup directory.')
    metadata=json.loads((directory/'metadata.json').read_text())
    backup=directory/'original-template'
    if metadata['uid'] != uid or metadata['finger'] != finger or backup.is_symlink():
        raise SystemExit('Backup ownership/finger mismatch.')
    if hashlib.sha256(backup.read_bytes()).hexdigest() != metadata['sha256']:
        raise SystemExit('Backup checksum mismatch.')
    if action == 'stage':
        candidate=directory/'candidate-template'
        if candidate.exists() or original.is_symlink() or not original.is_file():
            raise SystemExit('Expected new candidate missing or already staged.')
        shutil.copyfile(original,candidate)
        metadata.update(candidate_sha256=hashlib.sha256(candidate.read_bytes()).hexdigest(),state='testing')
        (directory/'metadata.json').write_text(json.dumps(metadata))
        print('Candidate saved in the protected backup.',flush=True)
        return
    if action == 'keep':
        if original.is_symlink() or hashlib.sha256(original.read_bytes()).hexdigest() != metadata.get('candidate_sha256'):
            raise SystemExit('Candidate changed during validation.')
        metadata['state']='kept-after-validation'
        (directory/'metadata.json').write_text(json.dumps(metadata))
        print('Validated candidate retained.',flush=True)
        return
    # Preserve a candidate even if enrollment failed or was cancelled before stage.
    if original.is_file() and not original.is_symlink() and not (directory/'candidate-template').exists():
        if hashlib.sha256(original.read_bytes()).hexdigest() != metadata['sha256']:
            shutil.copyfile(original,directory/'candidate-template')
    subprocess.run(['systemctl','stop','fprintd.service'],check=True)
    try:
        if original.is_symlink():
            raise SystemExit('Unexpected target link.')
        original.parent.mkdir(parents=True,mode=0o700,exist_ok=True)
        temp=original.with_name(original.name+'.guided-new')
        fd=os.open(temp,os.O_CREAT|os.O_EXCL|os.O_WRONLY,0o600)
        try:
            with os.fdopen(fd,'wb') as stream: stream.write(backup.read_bytes())
            os.replace(temp,original)
        finally: temp.unlink(missing_ok=True)
    finally: subprocess.run(['systemctl','start','fprintd.service'],check=True)
    metadata['state']='restored-original'
    (directory/'metadata.json').write_text(json.dumps(metadata))
    print('Previous selected enrollment restored; other fingers preserved.',flush=True)

if __name__ == '__main__': main()
