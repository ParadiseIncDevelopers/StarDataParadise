"""Classic adapters for the current StarDataParadise compatibility contracts.

No credentials leave the editor. SQL values use UTF-8 hex literals, preserving
BIGINT/DECIMAL values and working with either NO_BACKSLASH_ESCAPES setting.
"""

from decimal import Decimal
import json
import math
from pathlib import Path
import re


def identifier(value):
    if not isinstance(value, str) or not value or len(value) > 64 or '\0' in value:
        raise ValueError('Geçerli bir SQL nesne adı gerekli (1–64 karakter).')
    return '`' + value.replace('`', '``') + '`'


def literal(value):
    if value is None:
        return 'NULL'
    if isinstance(value, bool):
        return '1' if value else '0'
    if isinstance(value, (int, float, Decimal)):
        if isinstance(value, Decimal) and not value.is_finite() or isinstance(value, float) and not math.isfinite(value):
            raise ValueError('Sonlu bir sayısal değer gerekli.')
        return str(value)
    if isinstance(value, str):
        return "CONVERT(X'%s' USING utf8mb4)" % value.encode('utf-8').hex()
    raise ValueError('Desteklenmeyen SQL parametre türü.')


def bind(statement, parameters):
    """Bind helper placeholders, skipping quoted strings, identifiers/comments."""
    parameters = iter(parameters)
    output, i, quote = [], 0, None
    while i < len(statement):
        char = statement[i]
        if quote:
            output.append(char)
            if char == quote:
                if i + 1 < len(statement) and statement[i + 1] == quote:
                    output.append(statement[i + 1]); i += 1
                else:
                    quote = None
            elif char == '\\' and i + 1 < len(statement):
                output.append(statement[i + 1]); i += 1
        elif char in "'\"`":
            quote = char; output.append(char)
        elif statement.startswith('--', i) or char == '#':
            end = statement.find('\n', i)
            if end < 0:
                output.append(statement[i:]); i = len(statement); continue
            output.append(statement[i:end]); i = end; continue
        elif statement.startswith('/*', i):
            end = statement.find('*/', i + 2)
            if end < 0:
                raise ValueError('Tamamlanmamış SQL açıklaması.')
            output.append(statement[i:end + 2]); i = end + 2; continue
        elif char == '?':
            try:
                output.append(literal(next(parameters)))
            except StopIteration:
                raise ValueError('Eksik SQL parametresi.') from None
        else:
            output.append(char)
        i += 1
    try:
        next(parameters)
    except StopIteration:
        return ''.join(output)
    raise ValueError('Fazla SQL parametresi.')


def integer(value, minimum=0, maximum=(1 << 64) - 1, optional=False):
    value = str(value).strip()
    if optional and not value:
        return None
    if not re.fullmatch(r'[0-9]+', value, re.ASCII) or not minimum <= int(value) <= maximum:
        raise ValueError('%s–%s aralığında tam sayı gerekli.' % (minimum, maximum))
    return int(value)


class Rows:
    def __init__(self, rows=(), columns=(), affected=0):
        self.rows, self.columns, self.affected = list(rows), list(columns), affected
        self.cursor = 0

    def fetch_one(self):
        if self.cursor >= len(self.rows):
            return None
        row = self.rows[self.cursor]; self.cursor += 1
        return row

    def fetch_all(self):
        rows = self.rows[self.cursor:]; self.cursor = len(self.rows)
        return rows

    def get_affected_items_count(self):
        return self.affected


class ManagementSession:
    """mysqlsh Session subset for bundled helpers, using editor's aux connection.

    The auxiliary connection is distinct from the user's query transaction. Its
    autocommit is checked before every helper operation that can write.
    """
    def __init__(self, editor):
        self.editor = editor
        self.training_source = None
        self.training_numeric = set()

    def run_sql(self, statement, parameters=()):
        sql = bind(statement, parameters)
        if re.match(r'\s*(SELECT|SHOW|CALL|WITH|EXPLAIN)\b', sql, re.I):
            result = self.editor.executeManagementQuery(sql, 0)
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
                        value = int(value) if re.fullmatch(r'-?[0-9]+', value, re.ASCII) else Decimal(value)
                    elif (value is not None and self.training_source is not None and self.training_source in sql
                          and column.name.casefold() in self.training_numeric):
                        # Native GRT labels DECIMAL as string to protect precision.
                        # Convert only columns verified against source metadata.
                        value = Decimal(value)
                    row.append(value)
                rows.append(tuple(row)); present = result.nextRow()
            return Rows(rows, [c.name for c in columns])
        affected = self.editor.executeManagementCommand(sql, 0)
        return Rows(affected=affected)

    def require_autocommit(self):
        row = self.run_sql('SELECT @@autocommit').fetch_one()
        if row is None or row[0] != 1:
            raise ValueError('Araç bağlantısında autocommit açık olmalıdır. Açık işlem tamamlandıktan sonra yeniden deneyin.')

    def require_tls(self):
        row = self.run_sql("SHOW SESSION STATUS LIKE 'Ssl_cipher'").fetch_one()
        if row is None or len(row) < 2 or not row[1]:
            raise ValueError('Compute işletim araçları için TLS bağlantısı gerekli. Profilin TLS ayarlarını yapılandırın.')

    def configure_training_types(self, schema, table, columns):
        types = self.run_sql('SELECT COLUMN_NAME,DATA_TYPE FROM information_schema.COLUMNS '
                             'WHERE TABLE_SCHEMA=? AND TABLE_NAME=?', [schema, table]).fetch_all()
        available = {name.casefold(): kind.casefold() for name, kind in types}
        numeric = {'tinyint', 'smallint', 'mediumint', 'int', 'integer', 'bigint', 'decimal', 'numeric', 'float', 'double', 'real'}
        names = {name.casefold() for name in columns}
        if any(available.get(name) not in numeric for name in names):
            raise ValueError('Eğitim özellikleri ve etiket sütunu sayısal SQL türü taşımalıdır.')
        self.training_source = 'FROM %s.%s ' % (identifier(schema), identifier(table))
        self.training_numeric = names

    def clear_training_types(self):
        self.training_source = None
        self.training_numeric = set()


def history_sql(journal, mode, start='', end='', row='', offset='0', limit='32'):
    prefix = identifier(journal)
    start = integer(start, optional=True)
    end = integer(end, optional=True)
    row = integer(row, minimum=1, optional=True)
    offset, limit = integer(offset), integer(limit, minimum=1, maximum=256)
    if mode == 'log':
        args = (start, end, row, None, None, offset, limit)
        return 'CALL %s.history_log(%s);' % (prefix, ', '.join(map(literal, args)))
    if mode == 'read' and end is not None:
        args = (end, row, offset, limit)
        return 'CALL %s.history_read(%s);' % (prefix, ', '.join(map(literal, args)))
    if mode == 'diff' and start is not None and end is not None and start < end:
        args = (start, end, row, offset, limit)
        return 'CALL %s.history_diff(%s);' % (prefix, ', '.join(map(literal, args)))
    raise ValueError('İmaj için bitiş revizyonu, fark için artan iki revizyon gerekli.')


def verify_archive(path):
    """Use the provider's full checksum/shape checks; never trust a UI badge."""
    import sdp_compat_logical
    import sdp_compat_physical
    archive = Path(path).absolute()
    manifest = json.loads((archive / 'manifest.json').read_text(encoding='utf-8'))
    if manifest.get('format') == sdp_compat_logical.FORMAT:
        return sdp_compat_logical.read_manifest(archive)
    if manifest.get('format') == sdp_compat_physical.FORMAT:
        return sdp_compat_physical.manifest_read(archive)
    raise ValueError('Desteklenen StarDataParadise arşiv manifesti bulunamadı.')
