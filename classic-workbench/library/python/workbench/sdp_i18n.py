"""Shared, offline UI language catalogs for StarDataParadise Classic.

Only presentation literals belong here. Never translate SQL, identifiers,
protocol fields, variable names or values returned by a server.
"""
import json
import os
from pathlib import Path
from threading import RLock

_lock = RLock()
_messages = None
_sources = None
_language = None


def _key(value):
    return value.replace("&", "").replace("_", "")


def _load():
    global _messages, _sources, _language
    if _messages is not None:
        return
    # Native bootstrap sets this before Python initializes. During plugin
    # imports GRT still contains defaults, not the user's saved options.
    language = os.environ.get("SDP_UI_LANGUAGE")
    if language is None:
        try:
            import grt
            language = grt.root.wb.options.options.get("workbench:UILanguage", "tr")
        except (ImportError, AttributeError, KeyError, TypeError):
            language = "tr"
    _language = "en" if language == "en" else "tr"
    here = Path(__file__).resolve()
    candidates = [Path(os.environ["SDP_UI_LOCALES"])] if os.environ.get("SDP_UI_LOCALES") else []
    candidates += [here.parent.parent / "locales", here.parents[3] / "res" / "locales"]
    directory = next((path for path in candidates if path.is_dir()), None)
    messages, sources = {}, {}
    if directory:
        for path in sorted(directory.glob("tr*.json")):
            try:
                catalog = json.loads(path.read_text(encoding="utf-8-sig"))
                if not isinstance(catalog, dict):
                    continue
                for source, target in catalog.items():
                    if isinstance(source, str) and isinstance(target, str):
                        messages[_key(source)] = target
                        sources.setdefault(_key(target), source)
            except (OSError, ValueError, TypeError):
                continue
    _messages, _sources = messages, sources


def tr(source):
    """Translate one UI literal; unknown strings fall back unchanged."""
    if not isinstance(source, str) or not source:
        return source
    with _lock:
        _load()
        if _language == "en" and _key(source) in _messages:
            return source
        return (_sources if _language == "en" else _messages).get(_key(source), source)


def current_language():
    """Return the session language; changing Preferences requires restart."""
    with _lock:
        _load()
        return _language


_ = tr
