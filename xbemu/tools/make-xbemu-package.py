#!/usr/bin/env python3
"""Builds the full XBemu package for Windows, from nothing but our own files.

usage: make-xbemu-package.py OUT --xemu EXE --launcher EXE --license FILE
                             --blank-hdd QCOW2 --oxos-bin BIN
                             --build N --commit SHA --version V [--notes TEXT]

Makes OUT/XBemu/ and OUT/XBemu-win-<build>-full.zip. There are no Microsoft
files in it: the BIOS is OXOS (GPL-2.0), the hard disk is the blank one from
make-blank-hdd.py, and the XBL Dashboard comes from its installer disc,
shipped with its creator's permission. The packaging stops when a file that
must never ship is in the folder.
"""
import argparse
import datetime
import hashlib
import os
import shutil
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
PACKAGE = os.path.join(HERE, '..', 'package')

# Never in an XBemu package: Microsoft's BIOS, boot ROM and dashboard discs,
# a console that was used, and game update discs
FORBIDDEN = ('complex_4627', 'mcpx', 'eeprom.bin', 'xbox_hdd.qcow2',
             'rainbow six', 'black arrow', 'xbl dashboard.iso',
             'xbl dashboard loader.iso', 'xboxdash')
ALLOWED = ('system/fresh/xbox_hdd.qcow2',)


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def put(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    for name in ('xemu', 'launcher', 'license', 'blank_hdd', 'oxos_bin',
                 'build', 'commit', 'version'):
        ap.add_argument('--' + name.replace('_', '-'), required=True)
    ap.add_argument('--notes', default='First XBemu build')
    ap.add_argument('--channel', default='stable',
                    choices=('stable', 'beta', 'test'))
    ap.add_argument('--date', default=datetime.date.today().isoformat())
    a = ap.parse_args()

    pin = dict(line.strip().split('=', 1) for line in
               open(os.path.join(PACKAGE, 'oxos.txt')) if '=' in line)
    if sha256(a.oxos_bin) != pin['sha256']:
        sys.exit('%s is not OXOS %s (oxos.txt)' % (a.oxos_bin, pin['tag']))

    pkg = os.path.join(a.out, 'XBemu')
    if os.path.exists(pkg):
        shutil.rmtree(pkg)
    sysd = os.path.join(pkg, 'system')
    put(a.launcher, os.path.join(pkg, 'XBemu.exe'))
    put(os.path.join(PACKAGE, 'README.txt'), os.path.join(pkg, 'README.txt'))
    put(a.xemu, os.path.join(sysd, 'xemu.exe'))
    put(a.license, os.path.join(sysd, 'LICENSE.txt'))
    for name in ('Start over.bat', 'shader-list.bin', 'oxos.txt',
                 'XBL Dashboard Installer.iso'):
        put(os.path.join(PACKAGE, name), os.path.join(sysd, name))
    put(os.path.join(PACKAGE, 'Launcher.cs'),
        os.path.join(sysd, 'Launcher-source.cs'))
    put(a.oxos_bin, os.path.join(sysd, 'bios', 'oxos-%s.bin' % pin['tag']))
    put(a.blank_hdd, os.path.join(sysd, 'fresh', 'xbox_hdd.qcow2'))
    os.makedirs(os.path.join(pkg, 'games'))
    with open(os.path.join(pkg, 'games', 'Put your game disc images here.txt'),
              'w') as f:
        f.write('Copy your game disc images (.iso) into this folder.\n')
    xemu = a.version.lstrip('v').split('-')[0]
    # The version line ("0.1") is in xbemu/VERSION
    with open(os.path.join(PACKAGE, '..', 'VERSION')) as f:
        release = f.read().strip()
    with open(os.path.join(sysd, 'version.txt'), 'w', newline='\n') as f:
        f.write('build=%s\nrelease=%s\nchannel=%s\nxemu=%s\nversion=%s\n'
                'date=%s\ncommit=%s\nupstream=v%s\nnotes=%s\n' %
                (a.build, release, a.channel, xemu, a.version, a.date,
                 a.commit, xemu, a.notes))

    files = []
    for root, _, names in os.walk(pkg):
        for n in names:
            rel = os.path.relpath(os.path.join(root, n), pkg).replace(os.sep, '/')
            low = rel.lower()
            if rel not in ALLOWED and any(bad in low for bad in FORBIDDEN):
                sys.exit('STOP: %s must never be in an XBemu package' % rel)
            files.append(rel)

    zpath = os.path.join(a.out, 'XBemu-win-%s-full.zip' % a.build)
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED,
                         compresslevel=6) as z:
        for rel in sorted(files):
            z.write(os.path.join(pkg, rel), 'XBemu/' + rel)
            print('  %10d  %s' % (os.path.getsize(os.path.join(pkg, rel)), rel))
    print(os.path.getsize(zpath) // (1024 * 1024), 'MB', zpath)
    print('sha256', sha256(zpath))


if __name__ == '__main__':
    main()
