"""Local installation handoff, pinned to one verified SQL owner and instance."""
import hashlib
import json
import os
from pathlib import Path
import struct
import time
import uuid


def manifest_path():
    return Path(os.environ['APPDATA']) / 'StarDataParadise/Workbench8.0.47/ai-worker-local.json'


def installation(identity, path=None):
    """identity is fetched by the selected editor, never supplied in a form."""
    manifest = json.loads(Path(path or manifest_path()).read_text(encoding='utf-8-sig'))
    if (manifest.get('version') != 1 or identity != [manifest.get('server_uuid'), str(manifest.get('port')), manifest.get('owner')]):
        raise ValueError('Yerel worker bu sunucu ve SQL hesabı için kurulmamış. Uzak worker yönetici yapılandırması gerektirir.')
    return manifest


def bind_credential(manifest, provider, reference, *, endpoint='', secret_env=''):
    # A custom proxy remains an explicit administrator trust decision.
    defaults = {'OPENAI': 'https://api.openai.com/v1/responses', 'ANTHROPIC': 'https://api.anthropic.com/v1/messages'}
    if provider not in defaults or endpoint and endpoint != defaults[provider] or secret_env:
        raise ValueError('Hızlı yerel kurulum resmi API ve Windows kasası kullanır; özel endpoint/ortam bağı için yönetici yapılandırması gerekli.')
    import re
    if not re.fullmatch(r'[A-Za-z0-9_.-]{1,128}', reference) or reference.lower().startswith('sk-'):
        raise ValueError('Geçerli bir kasa başvurusu gerekli.')
    path = Path(manifest['config'])
    value = json.loads(path.read_text(encoding='utf-8'))
    aliases = value.setdefault('credentials', {}).setdefault(manifest['owner'], {})
    binding = dict(source='windows', target='StarDataParadise/ai/' + reference, providers=[provider.lower()])
    if reference in aliases and aliases[reference] != binding:
        raise ValueError('Bu kasa başvurusu başka bir sağlayıcı/bağ için ayrılmış; yeni bir başvuru seçin.')
    aliases[reference] = binding
    temporary = path.with_name(path.name + '.' + uuid.uuid4().hex + '.tmp')
    try:
        temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def health(manifest):
    path = Path(manifest['status'])
    value = json.loads(path.read_text(encoding='utf-8'))
    # Timestamp freshness is checked against file mtime, avoiding locale parsing.
    if time.time() - path.stat().st_mtime > 20 or value.get('state') != 'ready':
        raise ValueError('Worker hazır değil veya son durum kaydı eski. Worker başlatıcısını ve kayıtlarını kontrol edin.')
    return value


def start_worker(manifest):
    import subprocess
    launcher = Path(manifest['launcher_exe']).resolve(strict=True)
    if launcher.parent != Path(manifest['config']).resolve(strict=True).parent or launcher.name != 'StarDataParadiseAiWorker.exe':
        raise ValueError('Yerel worker başlatıcısı kurulum kaydıyla eşleşmiyor.')
    subprocess.Popen([str(launcher), '--deployment', str(launcher.parent)],
        cwd=launcher.parent, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))


def stage_gguf(manifest, source, progress=None):
    """Copy user-selected bytes; source is never moved or modified."""
    source = Path(source)
    if source.suffix.lower() != '.gguf' or not source.is_file() or source.is_symlink():
        raise ValueError('Normal bir .gguf dosyası seçin.')
    root = Path(manifest['import_root']).resolve(strict=True)
    token = uuid.uuid4().hex
    temporary, target = root / (token + '.part'), root / (token + '.gguf')
    digest, size = hashlib.sha256(), 0
    try:
        with source.open('rb') as incoming, temporary.open('xb') as outgoing:
            original = os.fstat(incoming.fileno())
            if not 24 <= original.st_size <= manifest.get('maximum_import_bytes', 64 * 1024**3):
                raise ValueError('GGUF dosya boyutu izin verilen sınırın dışında.')
            header = incoming.read(24)
            magic, version, tensors, metadata = struct.unpack('<4sIQQ', header)
            if magic != b'GGUF' or version not in (2, 3) or tensors > 1_000_000 or metadata > 1_000_000:
                raise ValueError('Geçerli bir GGUF v2/v3 dosyası gerekli.')
            incoming.seek(0)
            for chunk in iter(lambda: incoming.read(1024 * 1024), b''):
                size += len(chunk)
                if size > original.st_size:
                    raise ValueError('Dosya kopyalama sırasında değişti.')
                digest.update(chunk); outgoing.write(chunk)
                if progress: progress(size, original.st_size)
            if size != original.st_size or os.fstat(incoming.fileno()).st_mtime_ns != original.st_mtime_ns:
                raise ValueError('Dosya kopyalama sırasında değişti.')
        temporary.replace(target)
        return dict(artifact=target.name, sha256=digest.hexdigest(), bytes=size)
    finally:
        temporary.unlink(missing_ok=True)
