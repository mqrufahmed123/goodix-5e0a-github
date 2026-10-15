#!/usr/bin/python3
"""Build and run the native dialog tests against a configured Settings tree.

Uses the tree's own compiler/linker flags. Device calls are mocked in the
test translation unit; no real fingerprint enrollment is started.
"""
import argparse
from pathlib import Path
import shlex
import subprocess

here = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('source_tree', type=Path)
parser.add_argument('--snapshot', type=Path)
args = parser.parse_args()
source = args.source_tree.resolve()
build = source / 'build'

subprocess.run(['ninja', '-C', str(build), 'shell/gnome-control-center'], check=True)
commands = subprocess.check_output(
    ['ninja', '-C', str(build), '-t', 'commands', 'shell/gnome-control-center'],
    text=True).splitlines()
compile_cmd = shlex.split(next(line for line in commands
    if ' -c ../panels/system/users/cc-fingerprint-dialog.c' in line))
link_cmd = shlex.split(next(line for line in commands
    if ' -o shell/gnome-control-center ' in line))

flags = []
skip = False
for item in compile_cmd:
    if skip:
        skip = False
        continue
    if item in ('-o', '-MQ', '-MF'):
        skip = True
        continue
    if item in ('-MD', '-c') or item.endswith('/cc-fingerprint-dialog.c'):
        continue
    flags.append(item)
obj = build / 'test-fingerprint-guide.o'
test = build / 'test-fingerprint-guide'
for name, filename in [('GUIDE', 'cc-fingerprint-guide.c'),
                       ('DIALOG', 'cc-fingerprint-dialog.c')]:
    flags.append(f'-DCC_FINGERPRINT_{name}_SOURCE="{source / "panels/system/users" / filename}"')
subprocess.run(flags + ['-c', str(here / 'test-dialog.c'), '-o', str(obj)],
               cwd=build, check=True)

link = []
skip = False
for item in link_cmd[1:]:
    if skip:
        skip = False
        continue
    if item == '-o':
        skip = True
        continue
    if item.endswith('.o'):
        continue
    link.append(item)
subprocess.run([link_cmd[0], str(obj), '-o', str(test)] + link,
               cwd=build, check=True)
subprocess.run([str(test)], check=True)
if args.snapshot:
    subprocess.run([str(test), '--snapshot', str(args.snapshot.resolve())], check=True)
