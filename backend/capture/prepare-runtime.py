"""Create a CLI-only Wireshark runtime from the signed upstream portable distribution.

PE import closure keeps only required DLLs; MMS/TPKT/COTP/GOOSE/SV dissectors
are built into libwireshark and do not need GUI, audio or Falco plugins.
The source directory is never modified. Output must not already exist.
"""
from pathlib import Path
import json
import shutil
import struct
import sys

source, target = map(lambda value: Path(value).resolve(), sys.argv[1:3])
if target.exists():
    raise SystemExit('Output already exists; choose a new staging directory')
target.mkdir(parents=True)

def imports(file):
    data = file.read_bytes()
    offset = struct.unpack_from('<I', data, 0x3c)[0]
    section_count = struct.unpack_from('<H', data, offset + 6)[0]
    optional_size = struct.unpack_from('<H', data, offset + 20)[0]
    optional = offset + 24
    directory = optional + (112 if struct.unpack_from('<H', data, optional)[0] == 0x20b else 96)
    sections = []
    for index in range(section_count):
        section = optional + optional_size + index * 40
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from('<IIII', data, section + 8)
        sections.append((virtual_address, max(virtual_size, raw_size), raw_offset))
    def physical(rva):
        for address, length, raw in sections:
            if address <= rva < address + length:
                return raw + rva - address
        return rva
    def name(rva):
        at = physical(rva)
        return data[at:data.index(b'\0', at)].decode('ascii').lower()
    result = []
    rva, size = struct.unpack_from('<II', data, directory + 8)
    if rva:
        entry = physical(rva)
        while any(data[entry:entry + 20]):
            result.append(name(struct.unpack_from('<I', data, entry + 12)[0]))
            entry += 20
    rva, size = struct.unpack_from('<II', data, directory + 13 * 8)
    if rva:
        entry = physical(rva)
        while any(data[entry:entry + 32]):
            result.append(name(struct.unpack_from('<I', data, entry + 4)[0]))
            entry += 32
    return result

files = {item.name.lower(): item for item in source.iterdir() if item.is_file()}
pending = ['tshark.exe', 'dumpcap.exe', 'editcap.exe', 'mergecap.exe', 'capinfos.exe']
copied = set()
while pending:
    name = pending.pop()
    if name in copied or name not in files:
        continue  # System/Npcap libraries are supplied by Windows/the capture driver.
    file = files[name]
    shutil.copy2(file, target / file.name)
    copied.add(name)
    pending.extend(imports(file))
for name in ['COPYING.txt', 'README.txt', 'README.xml-output', 'cfilters', 'dfilters', 'dmacros', 'wka', 'smi_modules', 'tshark.html', 'dumpcap.html', 'wireshark-filter.html', 'ws.css']:
    if (source / name).exists():
        shutil.copy2(source / name, target / name)
for name in ['diameter', 'dtds', 'generic', 'profiles', 'protobuf', 'radius', 'snmp', 'tpncp', 'wimaxasncp']:
    if (source / name).is_dir():
        shutil.copytree(source / name, target / name)
(target / 'studio-runtime.json').write_text(json.dumps({'upstream': 'Wireshark 4.6.8', 'tools': sorted(copied), 'source': 'https://www.wireshark.org/download.html', 'portable_sha256': 'd6cf5056586a0156c8634a9d9b3097e60a8dda408b889c9afc4445ec2b18d581'}, indent=2), encoding='utf-8')
print(f'{len(copied)} executable/DLL files; {sum(p.stat().st_size for p in target.rglob("*") if p.is_file()) / 1024 / 1024:.1f} MiB')
