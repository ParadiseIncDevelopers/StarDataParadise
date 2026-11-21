"""Native Classic AI and SQL workspaces for StarDataParadise."""
import json
import math

import grt
import mforms
from wb import DefineModule, wbinputs
from workbench.graphics.cairo_utils import Context

from sdp_tools_grt import ToolPage, box, button, pretty, text
from sdp_workspace_backend import (CATALOGS, WorkspaceBackend, conversation_create,
    conversation_script, dependency_nodes, document, grants_sql, knowledge_call,
    object_template, qualified, records, typed_value, version_nodes)
from sdp_workspace_backend import model_registration, model_worker_binding, run_statement, store_windows_credential
from sdp_workspace_backend import action_prepare, action_statement, tool_action_prepare
from sdp_workspace_backend import definition_text, object_edit_sql

ModuleInfo = DefineModule(name='SdpWorkspace', author='StarDataParadise', version='1.0')
_views = []


def code(source='', readonly=False, language=None):
    editor = mforms.newCodeEditor(None)
    editor.set_language(mforms.LanguageMySQL if language is None else language)
    editor.set_text(source)
    editor.set_read_only(readonly)
    return editor


def scrolled(view):
    scroll = mforms.newScrollPanel(0); scroll.add(view)
    return scroll


def table(columns, rows):
    grid = mforms.newTreeView(mforms.TreeFlatList | mforms.TreeAltRowColors)
    for name, width in columns:
        grid.add_column(mforms.StringColumnType, name, width, False)
    grid.end_columns()
    for values in rows:
        node = grid.add_node()
        for index, value in enumerate(values):
            node.set_string(index, '' if value is None else str(value))
    return grid


class FieldsDialog(mforms.Form):
    def __init__(self, title, fields, secret_fields=()):
        super().__init__(mforms.Form.main_form(), mforms.FormDialogFrame)
        self.set_title(title)
        content = box(); content.set_padding(12)
        self.fields = {}
        for key, label, value, choices in fields:
            line = box(True)
            caption = mforms.newLabel(label); caption.set_size(180, -1)
            line.add(caption, False, True)
            if choices:
                control = mforms.newSelector(mforms.SelectorCombobox); control.add_items(choices)
            else:
                control = mforms.newTextEntry(mforms.PasswordEntry if key in secret_fields else mforms.NormalEntry)
            control.set_value(str(value)); control.set_name(label)
            self.fields[key] = control; line.add(control, True, True)
            content.add(line, False, True)
        actions = box(True)
        self.ok = button('Devam', lambda: None); self.cancel = button('Vazgeç', lambda: None)
        actions.add_end(self.ok, False, False); actions.add_end(self.cancel, False, False)
        content.add(actions, False, True); self.set_content(content)
        self.set_size(650, max(200, 80 + len(fields) * 38))

    def accepted(self):
        self.center()
        return self.run_modal(self.ok, self.cancel)

    def value(self, key):
        return self.fields[key].get_string_value().strip()

    def raw_value(self, key):
        return self.fields[key].get_string_value()


class SourceDialog(mforms.Form):
    def __init__(self, title, source, readonly=False):
        super().__init__(mforms.Form.main_form(), mforms.FormDialogFrame)
        self.set_title(title)
        content = box(); content.set_padding(12)
        self.source = code(source, readonly)
        content.add(self.source, True, True)
        actions = box(True)
        self.choice = None
        self.cancel = button('Vazgeç', lambda: None)
        self.open = button('SQL sekmesinde aç', self.open_source)
        self.run = button('Çalıştır', lambda: None)
        self.run.set_enabled(not readonly)
        actions.add_end(self.run, False, False); actions.add_end(self.open, False, False)
        actions.add_end(self.cancel, False, False); content.add(actions, False, True)
        self.set_content(content); self.set_size(860, 560)

    def open_source(self):
        self.choice = 'open'; self.end_modal(True)

    def accepted(self):
        self.center()
        if self.run_modal(self.run, self.cancel):
            return self.choice or 'run'
        return None


class DefinitionDialog(mforms.Form):
    def __init__(self, title, source, language):
        super().__init__(mforms.Form.main_form(), mforms.FormDialogFrame)
        self.set_title(title)
        content=box();content.set_padding(12)
        self.source=code(source,language=language);content.add(self.source,True,True)
        actions=box(True)
        self.ok=button('SQL önizlemesi',lambda:None);self.cancel=button('Vazgeç',lambda:None)
        actions.add_end(self.ok,False,False);actions.add_end(self.cancel,False,False)
        content.add(actions,False,True);self.set_content(content);self.set_size(860,560)

    def accepted(self):
        self.center()
        return self.run_modal(self.ok,self.cancel)


class Diagram(mforms.PyDrawBox):
    """Native Cairo diagram; data comes exclusively from catalog responses."""
    colors = ((.04, .53, .45), (.18, .39, .79), (.49, .25, .71), (.65, .42, .12))

    def __init__(self, mode, callback=None):
        super().__init__()
        self.set_managed(); self.set_release_on_add(); self.set_instance(self)
        self.mode, self.callback, self.data, self.selected = mode, callback, [], None
        self.title = ''; self.set_size(700, 230)

    def update(self, value, title=''):
        self.data, self.title = value, title
        height = max(220, len(value) * 62 + 40)
        width = max(700, len(value) * 190 + 60) if self.mode == 'versions' else 700
        self.set_size(width, height); self.set_needs_repaint()

    def label(self, c, value, x, y, bold=False, limit=76):
        c.set_source_rgb(.06, .15, .16); c.set_font('Segoe UI', False, bold); c.set_font_size(13)
        c.move_to(x, y); c.show_text(str(value)[:limit])

    def rect(self, c, x, y, w, h, selected=False):
        c.set_source_rgb(*((.87, .92, .89) if selected else (.91, .94, .94)))
        c.rounded_rect(x, y, w, h, 4); c.fill()
        c.set_source_rgb(.64, .71, .74); c.rounded_rect(x + .5, y + .5, w - 1, h - 1, 4); c.stroke()

    def repaint(self, cr, x, y, w, h):
        c = Context(cr); c.set_source_rgb(1, 1, 1); c.paint(); c.set_line_width(1)
        if not self.data:
            self.label(c, 'Sunucudan yüklenmiş kayıt yok.', 14, 30)
            return
        if self.mode == 'dependencies':
            self.rect(c, 16, 65, 245, 48, True); self.label(c, self.title, 28, 94, True, 32)
            for index, item in enumerate(self.data):
                yy = 18 + index * 62
                c.set_source_rgb(.04, .34, .31); c.move_to(261, 89); c.line_to(290, 89)
                c.line_to(290, yy + 24); c.line_to(322, yy + 24); c.stroke()
                c.move_to(322, yy + 24); c.line_to(315, yy + 20); c.line_to(315, yy + 28); c.close_path(); c.fill()
                self.rect(c, 322, yy, 350, 48)
                caption = item['kind'] + ' - ' + item['name']
                if item.get('version') is not None:
                    caption += ' - Sürüm ' + str(item['version'])
                self.label(c, caption, 334, yy + 29, False, 44)
        elif self.mode == 'versions':
            count = len(self.data); step = max(150, min(250, (max(w, 700) - 70) / max(count, 1)))
            c.set_source_rgb(.04, .34, .31); c.move_to(35, 115); c.line_to(35 + step * (count - 1), 115); c.stroke()
            for index, item in enumerate(self.data):
                xx = 35 + index * step
                c.arc(xx, 115, 6, 0, 2 * math.pi); c.fill()
                self.label(c, 'Sürüm %s - %s' % (item['version'], item['state']), xx - 10, 70, True, 26)
                self.label(c, item['time'], xx - 10, 90, False, 26)
        else:
            raise ValueError('Diyagram türü geçersiz.')


class WorkspacePage(ToolPage):
    def __init__(self, owner):
        super().__init__(owner)
        self.backend = owner.backend
        self.action_count = 0
        self.remove(self.actions); self.remove(self.result_tabs); self.remove(self.status)
        self.action_group = box(); self.action_group.add(self.actions, False, True)
        self.add(self.action_group, False, True); self.add(self.result_tabs, True, True); self.add(self.status, False, True)

    def action(self, caption, callback):
        if self.action_count and self.action_count % 5 == 0:
            # Native box layouts do not wrap. Keep controls reachable at the
            # supported 1280 px desktop width instead of clipping the toolbar.
            self.actions = box(True)
            self.action_group.add(self.actions, False, True)
        self.action_count += 1
        return super().action(caption, callback)

    def review_sql(self, title, source):
        dialog = SourceDialog(title, source)
        choice = dialog.accepted()
        if choice:
            source = dialog.source.get_text(False)
            if choice == 'open':
                self.backend.open_sql(source)
                self.status.set_text('SQL ana bağlantının yeni sorgu sekmesinde açıldı.')
            else:
                self.backend.execute(source)
                self.status.set_text('SQL ana bağlantıya gönderildi; sonuçlar SQL editöründe gösterilir.')
        return choice


class CatalogPage(WorkspacePage):
    def __init__(self, owner, kind):
        self.kind = kind; self.title = kind
        self.description = 'Sunucunun yetkili katalog görünümü - seçili bağlantı'
        super().__init__(owner)
        self.field('schema', 'Şema', self.editor.defaultSchema or '')
        self.object = None
        self.context_target = None
        self.action('Yenile', self.refresh)
        self.action('Yeni', self.create)
        self.action('İncele', self.inspect)
        self.action('Tanımı düzenle', self.edit)
        self.action('Çalıştır', self.run_object)
        self.action('Yetkiler', self.permissions)
        self.definition = code('', True); self.result_tabs.add_page(self.definition, 'Tanım')
        self.dependencies = Diagram('dependencies'); self.result_tabs.add_page(scrolled(self.dependencies), 'Bağımlılıklar')
        self.versions = Diagram('versions'); self.result_tabs.add_page(scrolled(self.versions), 'Sürümler')
        self.signature = box(); self.result_tabs.add_page(self.signature, 'Parametreler ve sonuç')
        self.signature_children = []
        if kind == 'Knowledge':
            self.action('Doğrula', lambda: self.object_command('VALIDATE KNOWLEDGE'))
            self.action('Planı göster', lambda: self.object_command('EXPLAIN KNOWLEDGE'))
        elif kind == 'Konuşmalar':
            self.action('SQL ile çalıştır', self.ai_sql)
            self.action('Bellek', lambda: self.object_command('SHOW MEMORY'))
            self.action('Belleği sıfırla', self.flush_memory)
        elif kind == 'AI modelleri':
            self.action('Sağlayıcı kaydı', self.model_settings)

    def refresh(self):
        from sdp_tools_backend import Rows
        self.context_target = None
        result = self.backend.list_objects(self.kind)
        schema = self.value('schema')
        if schema and 'schema_name' in result.columns:
            position = result.columns.index('schema_name')
            result = Rows([row for row in result.rows if row[position] == schema], result.columns)
        self.show_rows(result)

    def selection_changed(self):
        super().selection_changed()
        self.object = None
        self.context_target = None

    def target(self):
        if not self.grid or not self.grid.get_selected_node():
            if self.context_target:
                return self.context_target
        row = dict(zip([column for column in self.grid_columns], self.selected()))
        if not row.get('name'):
            raise ValueError('Sunucu nesne adı döndürmedi.')
        return row.get('schema_name') or self.value('schema'), row['name']

    def show_rows(self, result):
        self.grid_columns = list(result.columns)
        super().show_rows(result)

    def inspect(self):
        schema, name = self.target()
        self.load_document(schema, name)

    def load_document(self, schema, name):
        self.context_target = (schema, name)
        self.object = self.backend.describe(self.kind, schema, name)
        self.details.set_value(pretty(self.object))
        self.definition.set_text(str(self.object.get('definition', '')))
        self.dependencies.update(dependency_nodes(self.object), '%s - %s' % (self.kind, name))
        self.versions.update(version_nodes(self.object))
        for child in self.signature_children:
            self.signature.remove(child)
        parameters = self.object.get('parameters', [])
        results = self.object.get('result_columns', [])
        self.signature_children = [mforms.newLabel('Parametreler - %s' % len(parameters)),
            table([('Ad', 180), ('Tür', 160), ('NULL', 70), ('Varsayılan', 200)],
                  [(item.get('name'), item.get('type'), item.get('nullable', False), item.get('default')) for item in parameters]),
            mforms.newLabel('Sonuç sütunları - %s' % len(results)),
            table([('Ad', 180), ('Tür', 160), ('NULL', 70)],
                  [(item.get('name'), item.get('type'), item.get('nullable', False)) for item in results])]
        for index, child in enumerate(self.signature_children):
            self.signature.add(child, index % 2 == 1, True)
        self.result_tabs.set_active_tab(2)
        self.status.set_text('Nesne belgesi sunucudan okundu.')

    def create(self):
        if self.kind == 'AI modelleri':
            return self.model_settings()
        fields = [('name', 'Nesne adı', '', None)]
        if self.kind == 'Konuşmalar':
            fields += [('model', 'Model', '', None), ('functions', 'Fonksiyonlar', 'NO FUNCTIONS', ['NO FUNCTIONS', 'WITH FUNCTIONS']),
                       ('automatic', 'Otomatik son cevap', 'Açık', ['Açık', 'Kapalı']), ('memory', 'Bellek', 'PERSISTENT', ['PERSISTENT', 'TEMPORARY']),
                       ('tools', 'İzinli araçlar (JSON)', '[]', None)]
        form = FieldsDialog('Yeni - ' + self.kind, fields)
        if not form.accepted():
            return
        if self.kind == 'Konuşmalar':
            tools=json.loads(form.value('tools'))
            if tools:
                self.backend.require('ai_tools_v1')
            source = conversation_create(self.value('schema'), form.value('name'), form.value('model'),
                form.value('functions') == 'WITH FUNCTIONS', form.value('automatic') == 'Açık', form.value('memory') == 'PERSISTENT',
                tools)
            self.owner.open_ai_sql(self.value('schema'), form.value('name'), source)
            return
        else:
            source = object_template(self.kind, self.value('schema'), form.value('name'))
        self.review_sql('Oluştur - SQL’i incele', source)

    def edit(self):
        self.inspect()
        schema,name=self.target()
        if self.kind=='Konuşmalar':
            settings=self.object.get('settings',{})
            form=FieldsDialog('Konuşma ayarları - '+name,[
                ('automatic','Otomatik son cevap','Açık' if settings.get('auto_append',True) else 'Kapalı',['Açık','Kapalı']),
                ('memory','Bellek','PERSISTENT' if settings.get('persistent',True) else 'TEMPORARY',['PERSISTENT','TEMPORARY'])])
            if form.accepted():
                self.review_sql('Konuşmayı güncelle - SQL önizlemesi',
                    'ALTER AI CONVERSATION %s WITH(auto_append=%s,persistent=%s);' % (
                        qualified(schema,name),'TRUE' if form.value('automatic')=='Açık' else 'FALSE',
                        'TRUE' if form.value('memory')=='PERSISTENT' else 'FALSE'))
            return
        language={'Knowledge':mforms.LanguageMySQL,'Behavior':mforms.LanguageNone,
                  'Yanıt şemaları':mforms.LanguageJson}[self.kind]
        form=DefinitionDialog('Tanımı düzenle - '+self.kind,definition_text(self.kind,self.object),language)
        if form.accepted():
            source=object_edit_sql(self.kind,schema,name,self.object,form.source.get_text(False))
            self.review_sql('Tanımı güncelle - SQL önizlemesi',source)

    def run_object(self):
        if self.kind == 'Konuşmalar':
            return self.ai_sql()
        self.inspect(); schema, name = self.target()
        if self.kind != 'Knowledge':
            raise ValueError('Bu nesne yürütülemez; tanım veya bağlı nesne üzerinden kullanılır.')
        params = self.object.get('parameters', [])
        fields = []
        for i, item in enumerate(params):
            fields.append((str(i), '%s - %s' % (item.get('name', i), item.get('type', 'TEXT')), '', None))
            if item.get('nullable', False):
                fields.append((str(i) + '_null', 'NULL seçimi', 'Değer', ['Değer', 'NULL']))
        form = FieldsDialog('Knowledge - Parametreler ve sonuç', fields)
        if form.accepted():
            values = [typed_value(form.raw_value(str(i)), item.get('type', 'TEXT'), item.get('nullable', False),
                                 item.get('nullable', False) and form.value(str(i) + '_null') == 'NULL')
                      for i, item in enumerate(params)]
            self.review_sql('Knowledge - Çalıştır', knowledge_call(schema, name, values))

    def object_command(self, verb):
        schema, name = self.target()
        if verb == 'SHOW MEMORY':
            return self.review_sql('Konuşma - Bellek', 'START AI CONVERSATION %s;\nSHOW MEMORY;\nFINISH;' % qualified(schema, name))
        self.review_sql('Knowledge - İncele', '%s %s;' % (verb, qualified(schema, name)))

    def ai_sql(self):
        schema, name = self.target()
        self.owner.open_ai_sql(schema, name)

    def flush_memory(self):
        schema, name = self.target()
        self.review_sql('Belleği sıfırla - SQL’i incele',
            '-- Mesaj geçmişi ve behavior tanımları silinmez.\nSTART AI CONVERSATION %s;\nFLUSH MEMORY;\nCOMMIT;\nFINISH;' % qualified(schema, name))

    def permissions(self):
        self.backend.require('ai_role_acl_v1')
        schema, name = self.target()
        form = FieldsDialog('Yetkiler - ' + name, [('role', 'Rol', '', None)])
        if form.accepted():
            singular = CATALOGS[self.kind][1]
            self.review_sql('Yetkiler - SQL’i incele',
                grants_sql(singular, schema, name, form.value('role'), execute=True))

    def model_settings(self):
        self.owner.select(4)


class SqlPage(WorkspacePage):
    title = 'SQL ve PL/SQL'
    description = 'Normal SQL editörü - kısa yazım ek mod gerektirmez'
    snippets = {
        'EXCLUDE / REPLACE': 'SELECT m.* EXCLUDE (dahili_not) REPLACE (LOWER(m.eposta) AS eposta)\nFROM musteriler AS m;',
        'QUALIFY': 'SELECT musteri_id, siparis_id FROM siparisler\nQUALIFY ROW_NUMBER() OVER (PARTITION BY musteri_id ORDER BY tarih DESC, siparis_id DESC) = 1;',
        'Aşamalı sorgu': 'FROM satislar\n|> EXTEND adet * birim_fiyat AS brut\n|> AGGREGATE SUM(brut) AS toplam GROUP BY sube_id\n|> ORDER BY toplam DESC;',
        'PL/SQL': 'CREATE PROCEDURE stok_kontrol(p_limit BIGINT)\nAS\n    LET adet = (SELECT COUNT(*) FROM urunler WHERE stok_miktari < :p_limit);\n    WHEN adet > 0 DO INSERT INTO uyarilar (adet) VALUES (:adet);\nEND;',
    }

    def __init__(self, owner):
        super().__init__(owner)
        self.field('snippet', 'Örnek', next(iter(self.snippets)), list(self.snippets))
        self.action('Örneği aç', self.sample)
        self.action('SQL sekmesinde aç', lambda: self.backend.open_sql(self.source.get_text(False)))
        self.action('Çalıştır', self.run_sql)
        self.action('Genişletilmiş gösterim', self.expanded)
        self.source = code(); self.result_tabs.add_page(self.source, 'SQL')
        self.expansion = code('', True); self.result_tabs.add_page(self.expansion, 'Genişletilmiş gösterim')
        self.sample()

    def sample(self):
        self.source.set_text(self.snippets[self.value('snippet')]); self.result_tabs.set_active_tab(2)

    def run_sql(self):
        self.review_sql('SQL - Çalıştır', self.source.get_text(False))

    def expanded(self):
        self.backend.require('short_sql_v1')
        result = self.backend.read.run_sql('SHOW EXPANDED\n' + self.source.get_text(False))
        self.expansion.set_text('\n'.join(str(value) for row in result.rows for value in row if value is not None))
        self.result_tabs.set_active_tab(3)


class AiSqlPage(SqlPage):
    title = 'AI SQL'
    description = 'Konuşmayı SQL betiğiyle çalıştırın - sonuçlar bağlı SQL editöründe gösterilir'
    snippets = {'Konuşma betiği': '-- Bir konuşma seçin veya yeni konuşma oluşturun.\n-- Prompt alanından SQL betiği hazırlayabilirsiniz.'}

    def __init__(self, owner):
        super().__init__(owner)
        self.field('schema', 'Şema', self.editor.defaultSchema or '')
        self.field('conversation', 'Konuşma adı')
        self.field('question', 'Prompt')
        self.field('mode', 'Çalıştırma', 'Asenkron', ['Asenkron', 'Senkron'])
        self.field('run', 'Run kimliği - boşsa son çalıştırma')
        self.action('Konuşma betiğini hazırla', self.prepare)
        self.action('Çalıştırmayı incele', lambda: self.run_command('DESCRIBE RUN'))
        self.action('Yanıtı getir', lambda: self.run_command('FETCH RESPONSE'))
        self.action('Çalıştırmayı iptal et', lambda: self.run_command('CANCEL RUN'))

    def prepare(self):
        self.backend.require('ai_conversation_v1')
        asynchronous = self.value('mode') == 'Asenkron'
        if not asynchronous:
            self.backend.require('ai_sync_v1')
        self.source.set_text(conversation_script(self.value('schema'), self.value('conversation'), self.value('question'), asynchronous))
        self.result_tabs.set_active_tab(2)

    def run_command(self, verb):
        self.backend.require('ai_conversation_v1')
        self.review_sql('AI SQL - ' + verb, run_statement(verb, self.value('run')))


class ModelsPage(WorkspacePage):
    title = 'AI modelleri'
    description = 'OpenAI API - Claude API - yerel GGUF modeli; anahtar yerine secret başvurusu'

    def __init__(self, owner):
        super().__init__(owner)
        self.field('schema', 'Şema', self.editor.defaultSchema or '')
        self.field('name', 'Model adı')
        self.field('provider', 'Sağlayıcı', 'OPENAI', ['OPENAI', 'ANTHROPIC', 'LOCAL_GGUF'])
        self.field('remote', 'Remote model / artifact kimliği')
        self.field('endpoint', 'API uç noktası')
        self.field('secret_env', 'Worker secret_env başvurusu')
        self.field('credential_ref', 'Windows kasa başvurusu')
        self.field('artifact', 'Worker model deposunda GGUF')
        self.field('sha256', 'Artifact SHA-256')
        self.field('context_tokens', 'Bağlam token sınırı', '4096')
        self.field('max_output_tokens', 'Çıktı token sınırı', '1024')
        self.action('Modelleri oku', self.refresh)
        self.action('Modeli incele', self.inspect)
        self.action('Kayıt SQL’ini incele', self.registration)
        self.action('API anahtarını kasaya kaydet', self.credentials)
        self.action('Worker bağını hazırla', self.worker_binding)
        self.status.set_text('Worker erişimi doğrulanmadı. Kasa kaydı yalnız bu Windows kullanıcısına aittir.')
        self.details.set_value('API anahtarını ayrı maskeli kasa penceresine girin. SQL yalnız secret_env veya credential_ref taşır.\n'
            'Kasa geçerli Windows kullanıcısına aittir. Uzak worker için yönetici tarafından güvenilir başvuru bağı gerekir; anahtar aktarılmaz.\n'
            'Worker bağını hazırla ile bağlı hesabınıza ait anahtarsız yapılandırmayı görün. Worker yöneticisi bunu --config dosyasına eklemelidir.\n'
            'Özel API uç noktaları worker yapılandırmasında ayrıca tam adres izni gerektirir.\n'
            'GGUF yolu worker model deposuna göredir; istemci dosyası sunucuya kendiliğinden aktarılmaz.')

    def credentials(self):
        form = FieldsDialog('API kimlik bilgisini kaydet', [
            ('reference', 'Kasa başvurusu', self.value('credential_ref'), None),
            ('secret', 'API anahtarı', '', None)], secret_fields=('secret',))
        if not form.accepted():
            form.fields['secret'].set_value(''); return
        try:
            store_windows_credential(form.value('reference'), form.raw_value('secret'))
            self.fields['credential_ref'].set_value(form.value('reference'))
            self.fields['secret_env'].set_value('')
            self.status.set_text('API anahtarı bu Windows kullanıcısının kasasına kaydedildi; worker bağı ayrıca gerekir.')
        finally:
            form.fields['secret'].set_value('')

    def worker_binding(self):
        row = self.backend.read.run_sql('SELECT STARDATAPARADISE_ACTOR();').fetch_one()
        if row is None:
            raise ValueError('Bağlı hesabın sunucu kimliği okunamadı.')
        value = model_worker_binding(row[0], self.value('provider'), endpoint=self.value('endpoint'),
            secret_env=self.value('secret_env'), credential_ref=self.value('credential_ref'))
        self.details.set_value('Worker erişimi doğrulanmadı. Bu bölüm güvenilir worker yöneticisinin --config dosyasına ekleyeceği başvuru bağıdır.\n'
            'Windows kaynağı: worker aynı Windows hesabıyla çalışmalıdır. Uzak veya farklı hesapla çalışan worker kendi kasasını kullanır; bu istemcinin anahtarı aktarılmaz.\n'
            'Ortam kaynağı: değişken worker hizmetinin ortamında bulunmalıdır.\n'
            'Yerel GGUF: göreli dosya import_roots altında worker üzerinde bulunmalıdır. İçe aktarma, hash doğrulama ve gerçek model yükleme tamamlanmadan model kullanılabilir sayılmaz.\n\n' + pretty(value))
        self.result_tabs.set_active_tab(1)
        self.status.set_text('Anahtarsız worker yapılandırması hazırlandı; erişim henüz doğrulanmadı.')

    def refresh(self):
        result = self.backend.list_objects('AI modelleri')
        self.grid_columns = result.columns; self.show_rows(result)

    def inspect(self):
        row = dict(zip(self.grid_columns, self.selected()))
        value = self.backend.describe('AI modelleri', row.get('schema_name') or self.value('schema'), row.get('name'))
        self.show_json(value)
        self.fields['name'].set_value(str(value.get('name', '')))

    def registration(self):
        self.backend.require('ai_models_v1')
        source = model_registration(self.value('schema'), self.value('name'), self.value('provider'),
            self.value('remote'), **{key: self.value(key) for key in
                ('endpoint', 'secret_env', 'credential_ref', 'artifact', 'sha256', 'context_tokens', 'max_output_tokens')})
        self.review_sql('AI model - Kayıt SQL’i', source)


class ApprovalPage(WorkspacePage):
    title = 'Yetkiler ve onay'
    description = 'Yetkiler ve onay kaydı çalıştırma sırasında sunucuda doğrulanır'

    def __init__(self, owner):
        super().__init__(owner)
        self.field('schema', 'Şema', self.editor.defaultSchema or '')
        self.field('name', 'Yordam', 'siparis_iptal')
        self.field('role', 'Rol')
        self.field('execute', 'EXECUTE', 'Evet', ['Evet', 'Hayır'])
        self.field('approve', 'APPROVE', 'Hayır', ['Evet', 'Hayır'])
        self.field('required', 'Onay gerekli', 'Evet', ['Evet', 'Hayır'])
        self.field('action_id', 'İşlem kimliği', '')
        self.action('Yetki SQL’i', self.grant)
        self.action('Yordam işlemi hazırla', self.prepare)
        self.action('Araç işlemi hazırla', self.prepare_tool)
        self.action('Önizlemeyi getir', self.inspect_action)
        self.action_buttons = [self.action('Onay SQL’i', lambda: self.action_sql('APPROVE')),
                               self.action('Ret SQL’i', lambda: self.action_sql('DENY')),
                               self.action('Yürütme SQL’i', lambda: self.action_sql('EXECUTE'))]
        self.action_document = None
        for key in ('action_id', 'schema', 'name'):
            self.fields[key].add_changed_callback(self.invalidate_action)
        self.invalidate_action()

    def grant(self):
        self.backend.require('ai_role_acl_v1')
        source = grants_sql('PROCEDURE', self.value('schema'), self.value('name'), self.value('role'),
            self.value('execute') == 'Evet', self.value('approve') == 'Evet', self.value('required') == 'Evet')
        self.review_sql('Yetkiler ve onay - ' + self.value('name'), source)

    def invalidate_action(self):
        self.action_document = None
        for control in self.action_buttons:
            control.set_enabled(False)

    def prepare(self):
        self.backend.require('ai_action_v1')
        form = FieldsDialog('İşlem hazırla - ' + self.value('name'),
                            [('arguments', 'Parametreler (JSON dizisi)', '[9001]', None)])
        if form.accepted():
            self.invalidate_action()
            self.fields['action_id'].set_value('')
            self.review_sql('İşlem hazırlama - SQL’i incele',
                action_prepare(self.value('schema'), self.value('name'), form.raw_value('arguments')))

    def prepare_tool(self):
        self.backend.require('ai_action_v1')
        form = FieldsDialog('Araç çağrısı - İşlem hazırla', [('call_id', 'Araç çağrı kimliği', '', None)])
        if form.accepted():
            self.invalidate_action()
            self.fields['action_id'].set_value('')
            self.review_sql('Araç işlemi hazırlama - SQL’i incele', tool_action_prepare(form.value('call_id')))

    def inspect_action(self):
        self.backend.require('ai_action_v1')
        self.invalidate_action()
        value = document(self.backend.main.run_sql(action_statement('DESCRIBE', self.value('action_id'))))
        identity = value.get('action_id', '')
        action_statement('DESCRIBE', identity)
        if not identity:
            raise ValueError('Sunucu işlem kimliği bildirmedi.')
        target = value.get('target', {})
        self.fields['schema'].set_value(target.get('schema_name', ''))
        self.fields['name'].set_value(target.get('name', ''))
        self.fields['action_id'].set_value(identity)
        self.action_document = value
        self.show_json(value)
        for control in self.action_buttons:
            control.set_enabled(value.get('state') not in ('executed', 'denied', 'expired', 'failed', 'cancelled', 'stale'))
        self.status.set_text('İşlem önizlemesi - %s - %s' % (identity, value.get('state', '')))

    def action_sql(self, verb):
        value = self.action_document
        target = value.get('target', {}) if value else {}
        if value is None or self.value('action_id').lower() != value['action_id'].lower() or \
           self.value('schema') != target.get('schema_name', '') or self.value('name') != target.get('name', ''):
            self.invalidate_action()
            raise ValueError('Önce işlem önizlemesini yükleyin.')
        # Bind the inspected immutable ID, even if a SQL tab changes the
        # session's @sdp_action_id variable before this button is used.
        source = action_statement(verb, self.action_document['action_id'])
        self.review_sql('İşlem - SQL’i incele', source)
        self.invalidate_action()


class WorkspaceView(mforms.AppView):
    names = ('Knowledge', 'Konuşmalar', 'Behavior', 'Yanıt şemaları', 'AI modelleri', 'SQL ve PL/SQL', 'Yetkiler ve onay', 'AI SQL')

    def __init__(self, editor, page=0):
        super().__init__(False, 'StarDataParadise çalışma alanı', 'sdp_workspace', False)
        self.editor, self.backend = editor, WorkspaceBackend(editor)
        self.set_spacing(0)
        layout = box(True); layout.set_spacing(0)
        sidebar = box(); sidebar.set_padding(8); sidebar.set_size(180, -1)
        self.tabs = mforms.newTabView(mforms.TabViewTabless)
        self.pages = [CatalogPage(self, kind) for kind in self.names[:4]]
        self.pages += [ModelsPage(self), SqlPage(self), ApprovalPage(self), AiSqlPage(self)]
        for index, item in enumerate(self.pages):
            self.tabs.add_page(item, self.names[index])
            sidebar.add(button(self.names[index], lambda index=index: self.select(index)), False, True)
        layout.add(sidebar, False, True); layout.add(self.tabs, True, True); self.add(layout, True, True)
        self.on_close(self.close_workspace); self.select(page)

    def select(self, index):
        self.tabs.set_active_tab(index); self.set_title(self.names[index])

    def open_ai_sql(self, schema, name, source=None):
        page = self.pages[7]; page.fields['schema'].set_value(schema); page.fields['conversation'].set_value(name)
        if source is not None:
            page.source.set_text(source)
            page.status.set_text('Yeni konuşma SQL’i hazır - çalıştırın veya ana SQL sekmesinde açın.')
        self.select(7)

    def close_workspace(self):
        if self in _views:
            _views.remove(self)
        return True


def open_workspace(editor, page=0):
    for view in _views:
        if view.editor == editor:
            view.select(page); mforms.fromgrt(editor.dockingPoint).select_view(view); return 0
    view = WorkspaceView(editor, page); dock = mforms.fromgrt(editor.dockingPoint)
    dock.dock_view(view, '', 0); dock.select_view(view); _views.append(view); return 0


@ModuleInfo.export(grt.INT, grt.classes.db_query_Editor, grt.STRING, grt.STRING, grt.STRING)
def openObject(editor, kind, schema, name):
    pages = {'knowledge': 0, 'conversation': 1, 'behavior': 2, 'response_schema': 3,
             'model': 4, 'language': 5, 'approval': 6, 'ai_sql': 7}
    if kind not in pages:
        raise ValueError('Çalışma alanı türü geçersiz.')
    open_workspace(editor, pages[kind])
    view = next(item for item in _views if item.editor == editor)
    page = view.pages[pages[kind]]
    if 'schema' in page.fields:
        page.fields['schema'].set_value(schema)
    if isinstance(page, CatalogPage):
        def load():
            page.refresh()
            if name:
                page.load_document(schema, name)
        page.guard(load)
    elif kind == 'model':
        def model_load():
            page.refresh()
            if name:
                page.fields['name'].set_value(name)
                page.show_json(page.backend.describe('AI modelleri', schema, name))
        page.guard(model_load)
    return 0


@ModuleInfo.plugin('sdp.workspace.open', caption='AI ve SQL', input=[wbinputs.currentSQLEditor()], pluginMenu='SQL/Utilities', accessibilityName='AI ve SQL')
@ModuleInfo.export(grt.INT, grt.classes.db_query_Editor)
def openWorkspace(editor):
    return open_workspace(editor)


@ModuleInfo.plugin('sdp.workspace.knowledge', caption='Stored Knowledge', input=[wbinputs.currentSQLEditor()], accessibilityName='Stored Knowledge')
@ModuleInfo.export(grt.INT, grt.classes.db_query_Editor)
def openKnowledge(editor):
    return openObject(editor, 'knowledge', editor.defaultSchema or '', '')


@ModuleInfo.plugin('sdp.workspace.conversations', caption='AI konuşmaları', input=[wbinputs.currentSQLEditor()], accessibilityName='AI konuşmaları')
@ModuleInfo.export(grt.INT, grt.classes.db_query_Editor)
def openConversations(editor):
    return openObject(editor, 'conversation', editor.defaultSchema or '', '')


@ModuleInfo.plugin('sdp.workspace.language', caption='SQL ve PL/SQL', input=[wbinputs.currentSQLEditor()], accessibilityName='SQL ve PL/SQL')
@ModuleInfo.export(grt.INT, grt.classes.db_query_Editor)
def openLanguage(editor):
    return open_workspace(editor, 5)


@ModuleInfo.plugin('sdp.workspace.behaviors', caption='Behavior', input=[wbinputs.currentSQLEditor()], accessibilityName='Behavior')
@ModuleInfo.export(grt.INT, grt.classes.db_query_Editor)
def openBehaviors(editor):
    return openObject(editor, 'behavior', editor.defaultSchema or '', '')


@ModuleInfo.plugin('sdp.workspace.models', caption='AI modelleri', input=[wbinputs.currentSQLEditor()], accessibilityName='AI modelleri')
@ModuleInfo.export(grt.INT, grt.classes.db_query_Editor)
def openModels(editor):
    return openObject(editor, 'model', editor.defaultSchema or '', '')


@ModuleInfo.plugin('sdp.workspace.responseSchemas', caption='Yanıt şemaları', input=[wbinputs.currentSQLEditor()], accessibilityName='Yanıt şemaları')
@ModuleInfo.export(grt.INT, grt.classes.db_query_Editor)
def openResponseSchemas(editor):
    return openObject(editor, 'response_schema', editor.defaultSchema or '', '')
