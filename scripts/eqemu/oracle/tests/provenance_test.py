#!/usr/bin/env python3
"""Keep vendored sources identifiable and the VM-side kill unambiguous."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
sources = {
    'eqpcap.py': '/home/user/eq-oracle/uigap-scripts/eqpcap.py',
    'step_frames.py': '/home/user/eq-oracle/uigap-scripts/step_frames.py',
    'check_ledgers.py': '/home/user/eq-oracle/uigap-scripts/check_ledgers.py',
    'patch_Titanium.conf': '/mnt/projects/akk-stack/code/utils/patches/patch_Titanium.conf',
}
for name, origin in sources.items():
    first_lines = (root / 'vendor' / name).read_text().splitlines()[:3]
    assert any('2026-10-13' in line and origin in line for line in first_lines), name
remote = (root / 'oracle_remote.sh').read_text()
assert 'VM-side pkill, not an eqemu-container pkill' in remote
print('PASS: vendored provenance and VM-side pkill scope')
