"""Native SQL contracts for the AI and language workspaces.

AI catalogs, active roles and stateful scripts use the editor's main connection,
so metadata permissions and conversation state follow the selected SQL session.
"""
import json
import re

from decimal import Decimal
from sdp_tools_backend import Rows, bind, identifier, literal


CATALOGS = {
    'Knowledge': ('KNOWLEDGES', 'KNOWLEDGE', 'stored_knowledge_v1'),
    'Konuşmalar': ('CONVERSATIONS', 'CONVERSATION', 'ai_conversation_v1'),
    'Behavior': ('BEHAVIORS', 'BEHAVIOR', 'behavior_v1'),
    'Yanıt şemaları': ('RESPONSE SCHEMAS', 'RESPONSE SCHEMA', 'response_schema_v1'),
    'AI modelleri': ('MODELS', 'MODEL', 'ai_models_v1'),
}


def qualified(schema, name):
    return (identifier(schema) + '.' if schema else '') + identifier(name)


def records(result):
    return [dict(zip(result.columns, row)) for row in result.rows]


def document(result, column='document'):
    """Decode the native DESCRIBE document without discarding NULLs or numbers."""
    rows = records(result)
    if not rows:
        raise ValueError('Nesne bulunamadı veya görünür değil.')
    row = rows[0]
    key = next((key for key in row if key.casefold() == column), None)
    if key is None:
        raise ValueError('Sunucu DESCRIBE belge sözleşmesini bildirmedi.')
    value = json.loads(row[key], parse_float=Decimal) if isinstance(row[key], str) else row[key]
    if not isinstance(value, dict):
        raise ValueError('Sunucunun nesne belgesi geçersiz.')
    return value


def capability_enabled(capabilities, name):
    features = capabilities.get('features', {})
    value = features.get(name, capabilities.get(name, False))
    return value is True or isinstance(value, dict) and value.get('available') is True


def conversation_create(schema, name, model, functions=False, automatic=True, persistent=True, tools=()):
    if not isinstance(tools, (list, tuple)) or len(tools) > 64 or tools and not functions:
        raise ValueError('İzinli araçlar en fazla 64 kayıt olmalı; WITH FUNCTIONS seçilmelidir.')
    targets, seen = [], set()
    for tool in tools:
        if not isinstance(tool, dict) or set(tool) - {'kind', 'schema', 'name'} or tool.get('kind') not in ('KNOWLEDGE', 'FUNCTION', 'PROCEDURE'):
            raise ValueError('Her araç kind, schema ve name alanlarıyla tanımlanmalıdır.')
        key = (tool['kind'], tool.get('schema', schema), tool.get('name'))
        target = qualified(key[1], key[2])
        if key in seen:
            raise ValueError('Aynı araç birden fazla kez seçilemez.')
        seen.add(key); targets.append(key[0] + ' ' + target)
    function_clause = 'WITH FUNCTIONS' + (' (' + ', '.join(targets) + ')' if targets else '')
    return ('CREATE AI CONVERSATION %s\n    USING MODEL %s\n    %s\n    %s\n    MEMORY %s\n'
            '    RETURNING TICKET INTO @ticket;') % (
        qualified(schema, name), qualified(schema, model),
        function_clause if functions else 'NO FUNCTIONS',
        'AUTO APPEND MESSAGE' if automatic else '', 'PERSISTENT' if persistent else 'TEMPORARY')


def conversation_script(schema, name, question, asynchronous=False):
    # The GRT API has no native parameter array. Encode the value into a session
    # variable; it cannot become SQL source, even under NO_BACKSLASH_ESCAPES.
    return ('SET @sdp_question = %s;\nSTART AI CONVERSATION %s;\n'
            'APPEND PROMPT @sdp_question;\nAPPEND RESPONSE;\nCOMMIT%s;\nFINISH;') % (
        literal(question), qualified(schema, name), ' ASYNC' if asynchronous else '')


def run_statement(verb, run_id=''):
    if verb not in ('DESCRIBE RUN', 'FETCH RESPONSE', 'CANCEL RUN'):
        raise ValueError('Çalıştırma işlemi geçersiz.')
    if run_id and not re.fullmatch(r'[0-9a-fA-F]{32}', run_id):
        raise ValueError('Run kimliği 32 onaltılık karakter olmalıdır.')
    # A validated hex ID has no quote/backslash characters. A direct SQL text
    # literal also fits DESCRIBE's legacy-compatible native grammar.
    return verb + ' ' + ("'" + run_id.lower() + "'" if run_id else '@sdp_ai_last_run_id') + ';'




def scalar_sql(value):
    if isinstance(value, Decimal):
        if not value.is_finite() or len(value.as_tuple().digits) + abs(value.as_tuple().exponent) > 1024:
            raise ValueError('Sayısal parametre SQL literal sınırını aşıyor.')
        # Exponent literals take MySQL's approximate number path.
        return format(value, 'f')
    return literal(value)


def knowledge_call(schema, name, parameters):
    return 'EXECUTE KNOWLEDGE %s(%s);' % (
        qualified(schema, name), ', '.join(scalar_sql(value) for value in parameters))


def model_registration(schema, name, provider, model='', endpoint='', secret_env='',
                       credential_ref='', artifact='', sha256='', context_tokens='4096', max_output_tokens='1024'):
    """Build provider metadata. Credential material is never an input here."""
    from pathlib import PurePosixPath
    from urllib.parse import urlsplit
    target = qualified(schema, name)
    if provider not in ('OPENAI', 'ANTHROPIC', 'LOCAL_GGUF'):
        raise ValueError('Model sağlayıcısı geçersiz.')
    output = typed_value(str(max_output_tokens), 'BIGINT')
    if not 1 <= output <= 131072:
        raise ValueError('Çıktı token sınırı 1–131072 arasında olmalıdır.')
    options = [('max_output_tokens', output)]
    if provider == 'LOCAL_GGUF':
        path = PurePosixPath(artifact.replace('\\', '/'))
        if not artifact or path.is_absolute() or '..' in path.parts or ':' in artifact or '\0' in artifact or path.suffix.lower() != '.gguf':
            raise ValueError('Worker model deposuna göreli bir .gguf yolu gerekli; üst dizin kullanılamaz.')
        if not re.fullmatch(r'[0-9a-fA-F]{64}', sha256):
            raise ValueError('Artifact SHA-256 değeri 64 onaltılık karakter olmalıdır.')
        context = typed_value(str(context_tokens), 'BIGINT')
        if not 512 <= context <= 131072:
            raise ValueError('Bağlam token sınırı 512–131072 arasında olmalıdır.')
        options += [('runtime', 'llama.cpp'), ('sha256', sha256.lower()), ('context_tokens', context)]
        prefix = 'IMPORT MODEL %s FROM FILE %s' % (target, literal(path.as_posix()))
    else:
        if not model or len(model) > 256 or '\0' in model:
            raise ValueError('Sağlayıcının model kimliği gerekli (en fazla 256 karakter).')
        if bool(secret_env) == bool(credential_ref):
            raise ValueError('secret_env veya Windows kasa başvurusundan birini seçin.')
        if secret_env and not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', secret_env):
            raise ValueError('Secret alanı yalnız ortam değişkeninin adını kabul eder.')
        if credential_ref and (not re.fullmatch(r'[A-Za-z0-9_.-]{1,128}', credential_ref) or credential_ref.lower().startswith('sk-')):
            raise ValueError('Kasa başvurusu geçersiz.')
        options += [('secret_env' if secret_env else 'credential_ref', secret_env or credential_ref)]
        if endpoint:
            try:
                parsed = urlsplit(endpoint)
                port = parsed.port
            except ValueError:
                raise ValueError('API uç noktası kullanıcı bilgisi içermeyen bir HTTPS adresi olmalıdır.') from None
            if parsed.scheme != 'https' or not parsed.hostname or parsed.username or parsed.password or parsed.fragment or parsed.query or (port is not None and not 1 <= port <= 65535):
                raise ValueError('API uç noktası kullanıcı bilgisi içermeyen bir HTTPS adresi olmalıdır.')
            options += [('endpoint', endpoint)]
        prefix = 'CREATE MODEL %s TYPE GENERATIVE PROVIDER %s MODEL %s' % (target, provider, literal(model))
    return prefix + ' WITH (' + ', '.join(key + ' = ' + literal(value) for key, value in options) + ');'


def model_worker_binding(owner, provider, *, endpoint='', secret_env='', credential_ref=''):
    """Prepare trusted-worker configuration, without reading any credential.

    A client cannot attest another machine's service identity or its private
    configuration. This fragment is a concrete administrator handoff, not a
    readiness claim or an automatic server configuration mutation.
    """
    if not isinstance(owner, str) or not owner or len(owner) > 576 or '\0' in owner:
        raise ValueError('Bağlı sunucudan doğrulanmış hesap kimliği gerekli.')
    if provider == 'LOCAL_GGUF':
        return {'local_runtime': {'import_roots': {owner: '<worker üzerinde izinli GGUF dizini>'},
                'model_store': '<worker üzerinde model deposu>', 'executable': '<llama-server.exe tam yolu>'}}
    # Reuse exactly the SQL form's validation, including endpoint transport and
    # alias restrictions. Never accept the API-key value in this function.
    model_registration('', 'binding_validation', provider, 'binding-validation', endpoint=endpoint,
                       secret_env=secret_env, credential_ref=credential_ref)
    binding = ({'source': 'environment', 'name': secret_env} if secret_env else
               {'source': 'windows', 'target': 'StarDataParadise/ai/' + credential_ref})
    binding['providers'] = [provider.lower()]
    defaults = {'OPENAI': 'https://api.openai.com/v1/responses', 'ANTHROPIC': 'https://api.anthropic.com/v1/messages'}
    if endpoint and endpoint != defaults[provider]:
        binding['endpoints'] = [endpoint]
    return {'credentials': {owner: {secret_env or credential_ref: binding}}}


def typed_value(value, kind, nullable=False, is_null=False):
    if is_null:
        if not nullable:
            raise ValueError('Bu parametre NULL kabul etmiyor.')
        return None
    declaration = str(kind).upper().strip()
    token = re.match(r'[A-Z]+', declaration)
    normalized = token.group() if token else ''
    unsigned = bool(re.search(r'\b(?:UNSIGNED|ZEROFILL)\b', declaration))
    widths = dict(TINYINT=8, SMALLINT=16, MEDIUMINT=24, INT=32, INTEGER=32, BIGINT=64)
    if normalized in widths:
        if not re.fullmatch(r'[+-]?[0-9]+', value):
            raise ValueError('Tam sayı parametresi gerekli.')
        result = int(value)
        bits = widths[normalized]
        lower, upper = (0, (1 << bits) - 1) if unsigned else (-(1 << (bits - 1)), (1 << (bits - 1)) - 1)
        if not lower <= result <= upper:
            raise ValueError('Tam sayı parametresi %s aralığı dışında.' % declaration)
        return result
    if normalized in ('DECIMAL', 'NUMERIC', 'REAL', 'DOUBLE', 'FLOAT'):
        from decimal import Decimal, InvalidOperation
        try:
            result = Decimal(value)
        except InvalidOperation:
            raise ValueError('Geçerli sayısal parametre gerekli.') from None
        if not result.is_finite():
            raise ValueError('Sonlu sayısal parametre gerekli.')
        if unsigned and result < 0:
            raise ValueError('Unsigned parametre negatif olamaz.')
        return result
    if normalized in ('BOOL', 'BOOLEAN'):
        if value.casefold() not in ('true', 'false', '1', '0'):
            raise ValueError('Boolean parametre true veya false olmalıdır.')
        return value.casefold() in ('true', '1')
    return value


def object_template(kind, schema, name):
    target = qualified(schema, name)
    if kind == 'Knowledge':
        return ('CREATE KNOWLEDGE %s(p_id BIGINT)\nRETURNS TABLE (id BIGINT)\n'
                'READ ONLY\nSQL SECURITY INVOKER\nAS\nSELECT id FROM kaynak_tablo WHERE id = :p_id;') % target
    if kind == 'Behavior':
        return "CREATE BEHAVIOR %s\nAS 'Türkçe cevap ver.';" % target
    if kind == 'Yanıt şemaları':
        return ('CREATE RESPONSE SCHEMA %s VERSION 1\nAS JSON '
                "'{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}';") % target
    if kind == 'Konuşmalar':
        return conversation_create(schema, name, 'model_adi')
    raise ValueError('Bu nesne için sağlayıcı ayarlarını kullanın.')


def _definition_string(value):
    if not isinstance(value, str) or not value:
        raise ValueError('Düzenlenebilir bir nesne tanımı gerekli.')
    try:
        if len(value.encode('utf-8')) > 1048576:
            raise ValueError('Nesne tanımı en fazla 1 MiB olmalıdır.')
    except UnicodeError:
        raise ValueError('Nesne tanımı geçerli Unicode olmalıdır.') from None
    return value


def _typed_json_text(value):
    """Bounded, deterministic JSON: native Decimal values remain JSON numbers."""
    chunks, size, nodes = [], 0, 0
    def emit(text):
        nonlocal size
        try:
            size += len(text.encode('utf-8'))
        except UnicodeError:
            raise ValueError('JSON geçerli Unicode olmalıdır.') from None
        if size > 1048576:
            raise ValueError('JSON tanımı en fazla 1 MiB olmalıdır.')
        chunks.append(text)
    def encode(item, depth=0):
        nonlocal nodes
        nodes += 1
        if depth > 64 or nodes > 32768:
            raise ValueError('JSON tanımı boyut veya derinlik sınırını aşıyor.')
        if item is None:
            emit('null')
        elif isinstance(item, bool):
            emit('true' if item else 'false')
        elif isinstance(item, Decimal):
            emit(scalar_sql(item))
        elif isinstance(item, (int, float, str)):
            try:
                emit(json.dumps(item, ensure_ascii=False, allow_nan=False))
            except (ValueError, OverflowError):
                raise ValueError('JSON sonlu ve desteklenen değerler içermelidir.') from None
        elif isinstance(item, (dict, list)):
            mapping = isinstance(item, dict)
            if mapping and any(not isinstance(key, str) for key in item):
                raise ValueError('JSON nesne anahtarları metin olmalıdır.')
            emit('{' if mapping else '[')
            entries = sorted(item) if mapping else range(len(item))
            for index, key in enumerate(entries):
                emit((',' if index else '') + '\n' + '  ' * (depth + 1))
                if mapping:
                    emit(json.dumps(key, ensure_ascii=False) + ': ')
                encode(item[key], depth + 1)
            if item:
                emit('\n' + '  ' * depth)
            emit('}' if mapping else ']')
        else:
            raise ValueError('JSON desteklenmeyen bir değer içeriyor.')
    encode(value)
    return ''.join(chunks)


def definition_text(kind, document):
    """Return editor text, preserving the native DESCRIBE definition's types."""
    if not isinstance(document, dict) or 'definition' not in document:
        raise ValueError('Sunucu düzenlenebilir bir nesne tanımı döndürmedi.')
    definition = document['definition']
    if kind in ('Knowledge', 'Behavior'):
        if isinstance(definition,str) and '\0' in definition:
            raise ValueError('Metin editörü NUL karakteri içeren tanımı kayıpsız gösteremez.')
        return _definition_string(definition)
    if kind == 'Yanıt şemaları' and isinstance(definition, dict):
        return _typed_json_text(definition)
    raise ValueError('Bu nesnenin tanımı bu ekranda düzenlenemez.')


def _knowledge_column_sql(column):
    if not isinstance(column, dict) or type(column.get('nullable', False)) is not bool:
        raise ValueError('Knowledge sütun metadata sözleşmesi geçersiz.')
    name, kind = identifier(column.get('name')), column.get('type')
    if not isinstance(kind, str) or len(kind) > 256:
        raise ValueError('Knowledge SQL türü geçersiz.')
    # Accept the native canonical_type vocabulary as a complete declaration.
    # A type is SQL source, so a semicolon blacklist cannot validate metadata.
    match = re.fullmatch(
        r'(TINYINT|SMALLINT|MEDIUMINT|INT|BIGINT|FLOAT|DOUBLE|DECIMAL|DATE|YEAR|TIME|DATETIME|TIMESTAMP|JSON|'
        r'VARCHAR|VARBINARY|CHAR|BINARY|TINYBLOB|MEDIUMBLOB|LONGBLOB|BLOB|TINYTEXT|MEDIUMTEXT|LONGTEXT|TEXT)'
        r'(?:\(([0-9]{1,5})(?:,([0-9]{1,2}))?\))?'
        r'( UNSIGNED)?( ZEROFILL)?'
        r'(?: CHARACTER SET (`?[A-Za-z0-9_]+`?) COLLATE (`?[A-Za-z0-9_]+`?))?',
        kind, re.IGNORECASE)
    if not match:
        raise ValueError('Knowledge SQL türü native metadata biçiminde olmalıdır.')
    base, width, scale, unsigned, zerofill, charset, collation = match.groups()
    base = base.upper()
    numeric = base in ('TINYINT', 'SMALLINT', 'MEDIUMINT', 'INT', 'BIGINT', 'FLOAT', 'DOUBLE', 'DECIMAL')
    text = base in ('VARCHAR', 'CHAR', 'TINYTEXT', 'MEDIUMTEXT', 'LONGTEXT', 'TEXT')
    if ((unsigned or zerofill) and not numeric) or (charset and not text):
        raise ValueError('Knowledge SQL türü metadata seçenekleri geçersiz.')
    if base == 'DECIMAL':
        valid = width is not None and scale is not None and 1 <= int(width) <= 65 and 0 <= int(scale) <= min(30, int(width))
    elif base in ('VARCHAR', 'VARBINARY', 'CHAR', 'BINARY'):
        valid = width is not None and scale is None and 1 <= int(width) <= (255 if base in ('CHAR', 'BINARY') else 65535)
    elif base in ('TIME', 'DATETIME', 'TIMESTAMP'):
        valid = width is not None and scale is None and 0 <= int(width) <= 6
    else:
        valid = width is None and scale is None
    if not valid:
        raise ValueError('Knowledge SQL türü uzunluk veya ölçek metadata değeri geçersiz.')
    # A quoted charset/collation token must have both quotes, not only one.
    if any(token and token.startswith('`') != token.endswith('`') for token in (charset, collation)):
        raise ValueError('Knowledge SQL karakter kümesi metadata değeri geçersiz.')
    return name + ' ' + kind + (' NULL' if column.get('nullable', False) else ' NOT NULL')


def object_edit_sql(kind, schema, name, document, edited_definition):
    """Build a reviewable edit of the selected object, never a bare stored body."""
    target = qualified(schema, name)
    if not isinstance(document, dict):
        raise ValueError('Sunucunun nesne belgesi geçersiz.')
    body = _definition_string(edited_definition)
    if kind == 'Knowledge':
        lists = []
        for key, minimum, maximum in [('parameters', 0, 256), ('result_columns', 1, 512)]:
            columns = document.get(key)
            if not isinstance(columns, list) or not minimum <= len(columns) <= maximum:
                raise ValueError('Knowledge parametre ve sonuç metadata listeleri gerekli.')
            names = [column.get('name') for column in columns if isinstance(column, dict)]
            if len(names) != len(columns) or any(not isinstance(item, str) for item in names) or len({item.casefold() for item in names}) != len(names):
                raise ValueError('Knowledge metadata sütun adları benzersiz olmalıdır.')
            lists.append(', '.join(_knowledge_column_sql(column) for column in columns))
        return 'ALTER KNOWLEDGE %s(%s)\nRETURNS TABLE (%s)\nREAD ONLY SQL SECURITY INVOKER\nAS\n%s\n;' % (target, lists[0], lists[1], body)
    if kind == 'Behavior':
        return 'ALTER BEHAVIOR %s\nAS %s;' % (target, literal(body))
    if kind == 'Yanıt şemaları':
        try:
            decoded = json.loads(body, parse_float=Decimal,
                parse_constant=lambda _: (_ for _ in ()).throw(ValueError()))
        except (ValueError, RecursionError):
            raise ValueError('Yanıt şeması geçerli bir JSON nesnesi olmalıdır.') from None
        if not isinstance(decoded, dict):
            raise ValueError('Yanıt şeması bir JSON nesnesi olmalıdır.')
        version = document.get('version')
        if type(version) is not int or not 1 <= version < (1 << 64) - 1:
            raise ValueError('Sunucu yanıt şemasının geçerli sürümünü bildirmedi.')
        return ("CALL sdp_ai.dispatch('alter','response_schema',%s,%s,"
                "JSON_OBJECT('version',%s,'schema',CAST(%s AS JSON)),@sdp_workspace_output);") % (
            literal(schema) if schema else 'DATABASE()', literal(name), version + 1, literal(body))
    raise ValueError('Bu nesnenin tanımı bu ekranda düzenlenemez.')


def grants_sql(kind, schema, name, role, execute=False, approve=False, require_approval=False):
    privileges = {'KNOWLEDGE': 'EXECUTE', 'PROCEDURE': 'EXECUTE', 'FUNCTION': 'EXECUTE',
                  'BEHAVIOR': 'USE', 'MODEL': 'USE', 'CONVERSATION': 'SELECT, APPEND', 'RESPONSE SCHEMA': 'USE'}
    if kind not in privileges:
        raise ValueError('Yetki hedef türü geçersiz.')
    target, role = qualified(schema, name), identifier(role)
    statements = []
    if execute:
        statements.append('GRANT %s ON %s %s TO ROLE %s;' % (privileges[kind], kind, target, role))
    if approve:
        if kind != 'PROCEDURE':
            raise ValueError('Onay yetkisi yalnız yordam içindir.')
        statements.append('GRANT APPROVE ON PROCEDURE %s TO ROLE %s;' % (target, role))
    if require_approval:
        if kind != 'PROCEDURE':
            raise ValueError('Onay gereksinimi yalnız yordam içindir.')
        statements.append('REQUIRE APPROVAL ON PROCEDURE %s;' % target)
    if not statements:
        raise ValueError('En az bir yetki veya onay işlemi seçin.')
    return '\n'.join(statements)


def action_statement(verb, action_id=''):
    if verb not in ('DESCRIBE', 'APPROVE', 'DENY', 'EXECUTE'):
        raise ValueError('İşlem komutu geçersiz.')
    if action_id and not re.fullmatch('[0-9a-fA-F]{32}', action_id):
        raise ValueError('İşlem kimliği 32 onaltılık karakter olmalıdır.')
    target = "'" + action_id.lower() + "'" if action_id else '@sdp_action_id'
    return verb + ' ACTION ' + target + ';'


def action_prepare(schema, name, arguments):
    if not isinstance(arguments, str) or len(arguments.encode('utf-8')) > 65536:
        raise ValueError('Parametre JSON dizisi en fazla 64 KiB olmalıdır.')
    try:
        values = json.loads(arguments, parse_float=Decimal,
                            parse_constant=lambda _: (_ for _ in ()).throw(ValueError()))
    except (ValueError, RecursionError):
        raise ValueError('Geçerli bir JSON parametre dizisi gerekli.') from None
    if not isinstance(values, list) or len(values) > 128 or any(isinstance(value, (dict, list)) for value in values):
        raise ValueError('En fazla 128 skaler parametre girin; bileşik türleri SQL önizlemesinde düzenleyebilirsiniz.')
    source = 'PREPARE ACTION\nCALL %s(%s)\nRETURNING ID INTO @sdp_action_id;' % (
        qualified(schema, name), ', '.join(scalar_sql(value) for value in values))
    return source + '\nSELECT @sdp_action_id AS action_id;\n' + action_statement('DESCRIBE')


def tool_action_prepare(call_id):
    if not isinstance(call_id, str) or not re.fullmatch('[0-9a-fA-F]{32}', call_id):
        raise ValueError('Araç çağrı kimliği 32 onaltılık karakter olmalıdır.')
    return "PREPARE ACTION FOR TOOL CALL '%s'\nRETURNING ID INTO @sdp_action_id;\nSELECT @sdp_action_id AS action_id;\n%s" % (
        call_id.lower(), action_statement('DESCRIBE'))


def dependency_nodes(value):
    items = value.get('dependencies', [])
    if not isinstance(items, list):
        raise ValueError('Bağımlılık listesi geçersiz.')
    return [dict(name=str(item.get('name', item.get('id', ''))),
                 kind=str(item.get('kind', item.get('type', 'Nesne'))),
                 version=item.get('version')) for item in items if isinstance(item, dict)]


def version_nodes(value):
    items = value.get('versions', [])
    if not isinstance(items, list):
        raise ValueError('Sürüm listesi geçersiz.')
    return [dict(version=item.get('version'), state=item.get('state', ''),
                 time=item.get('updated_at', item.get('created_at', '')))
            for item in items if isinstance(item, dict)]




class MetadataSession:
    """Materialize metadata using the selected GRT connection, preserving types."""
    def __init__(self, editor, management=True):
        self.editor, self.management = editor, management

    def run_sql(self, statement, parameters=()):
        source = bind(statement, parameters)
        query = self.editor.executeManagementQuery if self.management else self.editor.executeQuery
        result = query(source, 0)
        if result is None:
            return Rows()
        rows, columns = [], list(result.columns)
        present = result.goToFirstRow()
        while present:
            row = []
            for index, column in enumerate(columns):
                value = result.stringFieldValue(index)
                if value is not None and column.columnType == 'int':
                    value = int(value)
                elif value is not None and column.columnType in ('numeric', 'real', 'float'):
                    value = Decimal(value)
                row.append(value)
            rows.append(tuple(row)); present = result.nextRow()
        return Rows(rows, [column.name for column in columns])


def store_windows_credential(reference, secret):
    """Store a generic credential for the current Windows identity only.

    SQL contains only credential_ref. A remote worker needs its own explicit
    trusted binding; this operation does not transfer a credential to a server.
    """
    import os
    if os.name != 'nt':
        raise ValueError('Yerel kimlik bilgisi kasası yalnız Windows üzerinde kullanılabilir.')
    if not re.fullmatch(r'[A-Za-z0-9_.-]{1,128}', reference) or reference.lower().startswith('sk-'):
        raise ValueError('Kimlik başvurusu 1–128 harf, sayı, nokta, alt çizgi veya tire olmalıdır.')
    if not secret or len(secret.encode('utf-8')) > 2560:
        raise ValueError('Geçerli bir API anahtarı gerekli (en fazla 2560 UTF-8 bayt).')
    import ctypes
    from ctypes import wintypes
    class Credential(ctypes.Structure):
        _fields_ = [('Flags', wintypes.DWORD), ('Type', wintypes.DWORD),
            ('TargetName', wintypes.LPWSTR), ('Comment', wintypes.LPWSTR),
            ('LastWritten', wintypes.FILETIME), ('CredentialBlobSize', wintypes.DWORD),
            ('CredentialBlob', ctypes.POINTER(ctypes.c_ubyte)), ('Persist', wintypes.DWORD),
            ('AttributeCount', wintypes.DWORD), ('Attributes', ctypes.c_void_p),
            ('TargetAlias', wintypes.LPWSTR), ('UserName', wintypes.LPWSTR)]
    target = 'StarDataParadise/ai/' + reference
    payload = bytearray(secret.encode('utf-8'))
    buffer = (ctypes.c_ubyte * len(payload)).from_buffer(payload)
    credential = Credential(Type=1, TargetName=target, CredentialBlobSize=len(payload),
        CredentialBlob=buffer, Persist=2, UserName='StarDataParadise')
    api = ctypes.WinDLL('Advapi32', use_last_error=True)
    api.CredWriteW.argtypes = [ctypes.POINTER(Credential), wintypes.DWORD]
    api.CredWriteW.restype = wintypes.BOOL
    try:
        if not api.CredWriteW(ctypes.byref(credential), 0):
            raise OSError(ctypes.get_last_error(), 'Windows kimlik bilgisi kasasına yazılamadı.')
    finally:
        for index in range(len(payload)):
            payload[index] = 0
    return target


class WorkspaceBackend:
    def __init__(self, editor):
        self.editor = editor
        self.main = MetadataSession(editor, management=False)
        self.read = self.main
        self.capabilities = {}

    def refresh_capabilities(self):
        row = self.read.run_sql('SELECT STARDATAPARADISE_CAPABILITIES()').fetch_one()
        if row is None:
            raise ValueError('Sunucu yetenek yanıtı alınamadı.')
        value = json.loads(row[0]) if isinstance(row[0], str) else row[0]
        if not isinstance(value, dict):
            raise ValueError('Sunucu yetenek yanıtı geçersiz.')
        self.capabilities = value
        return value

    def require(self, name):
        if not self.capabilities:
            self.refresh_capabilities()
        if not capability_enabled(self.capabilities, name):
            raise ValueError('Bağlı sunucu bu özelliği kullanılabilir olarak bildirmiyor: ' + name)

    def list_objects(self, kind):
        plural, _, capability = CATALOGS[kind]
        self.require(capability)
        return self.read.run_sql('SHOW %s;' % plural)

    def describe(self, kind, schema, name):
        _, singular, capability = CATALOGS[kind]
        self.require(capability)
        return document(self.read.run_sql('DESCRIBE %s %s;' % (singular, qualified(schema, name))))


    def open_sql(self, source):
        query = self.editor.addQueryEditor()
        query.replaceContents(source)
        return query

    def execute(self, source):
        if not source.strip():
            raise ValueError('Çalıştırılacak SQL gerekli.')
        return self.editor.executeScriptAndOutputToGrid(source)
