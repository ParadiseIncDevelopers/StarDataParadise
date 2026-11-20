"""Audit actual PE/CLR bindings and bundled executables in an SDP-named runtime."""
import argparse
import fnmatch
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[3]
NAMES = ROOT / 'packaging/classic-workbench/artifact-names.json'


def pe_imports(path):
    data = path.read_bytes()
    if data[:2] != b'MZ':
        return []
    header = struct.unpack_from('<I', data, 0x3C)[0]
    assert data[header:header + 4] == b'PE\0\0', path
    sections, optional_size = struct.unpack_from('<H12xH', data, header + 6)
    optional = header + 24
    magic = struct.unpack_from('<H', data, optional)[0]
    directory = optional + (112 if magic == 0x20B else 96)
    table = optional + optional_size
    def offset(rva):
        for index in range(sections):
            section = table + index * 40
            virtual_size, virtual_address, size, raw = struct.unpack_from('<IIII', data, section + 8)
            if virtual_address <= rva < virtual_address + max(size, virtual_size):
                return raw + rva - virtual_address
        raise ValueError((str(path), rva))
    def name(rva):
        start = offset(rva)
        return data[start:data.index(b'\0', start)].decode('ascii')
    result = []
    for index, width, name_offset in ((1, 20, 12), (13, 32, 4)):
        rva, size = struct.unpack_from('<II', data, directory + index * 8)
        if not rva:
            continue
        cursor = offset(rva)
        end = cursor + size
        while cursor + width <= end and any(data[cursor:cursor + width]):
            result.append(name(struct.unpack_from('<I', data, cursor + name_offset)[0]))
            cursor += width
    return result


def run(runtime, output):
    output.mkdir(parents=True, exist_ok=True)
    names = json.loads(NAMES.read_text())
    patterns = [name.lower() + '.*' for name in names['libraries']] + [name.lower() for name in names['files']]
    legacy_files = [str(path.relative_to(runtime)) for path in runtime.rglob('*') if path.is_file()
                    and any(fnmatch.fnmatchcase(path.name.lower(), pattern) for pattern in patterns)]
    assert not legacy_files, legacy_files
    for directory in names['directories']:
        assert not (runtime / directory).exists(), directory
    imports, failures = {}, []
    for path in runtime.rglob('*'):
        if path.suffix.lower() not in ('.dll', '.exe', '.pyd'):
            continue
        imports[str(path.relative_to(runtime))] = pe_imports(path)
        for dependency in imports[str(path.relative_to(runtime))]:
            if dependency.lower() in {name.lower() + '.dll' for name in names['libraries']}:
                failures.append((str(path.relative_to(runtime)), dependency))
    assert not failures, failures
    required = [name for name in names['libraries'].values() if not name.startswith('wb.sdp.')]
    assert all((runtime / (name + '.dll')).is_file() for name in required), required
    for executable in ('sdp.exe', 'sdpdump.exe'):
        completed = subprocess.run([str(runtime / executable), '--version'], cwd=runtime, capture_output=True, text=True)
        (output / (executable + '-version.log')).write_text(completed.stdout + completed.stderr, encoding='utf-8')
        assert completed.returncode == 0, executable
    compiler = Path(os.environ['WINDIR']) / 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
    probe = output / 'ArtifactAssemblyProbe.exe'
    subprocess.run([str(compiler), '/nologo', '/target:exe', '/platform:x64', '/r:System.Web.Extensions.dll',
                    '/out:' + str(probe), str(Path(__file__).with_name('ArtifactAssemblyProbe.cs'))], check=True)
    shutil.copyfile(Path(__file__).with_name('ErrorDialogProbe.exe.config'), str(probe) + '.config')
    completed = subprocess.run([str(probe), str(runtime), str(NAMES)], capture_output=True, text=True)
    (output / 'assembly-bindings.log').write_text(completed.stdout + completed.stderr, encoding='utf-8')
    assert completed.returncode == 0, completed.stdout + completed.stderr
    report = {'ok': True, 'pe_files': len(imports), 'native_imports': sum(map(len, imports.values())),
              'legacy_product_files': legacy_files, 'legacy_product_imports': failures,
              'clr_audit': completed.stdout.strip(), 'renamed_dlls': required,
              'vendor_filename_contracts': names['vendor_files'],
              'artifact_sha256': {name + '.dll': hashlib.sha256((runtime / (name + '.dll')).read_bytes()).hexdigest()
                                  for name in required}, 'imports': imports}
    (output / 'artifact-bindings.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({key: value for key, value in report.items() if key not in ('imports', 'artifact_sha256')}, ensure_ascii=False))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT / 'out/classic-sdp-names-acceptance')
    args = parser.parse_args()
    run(args.runtime.resolve(), args.output.resolve())
