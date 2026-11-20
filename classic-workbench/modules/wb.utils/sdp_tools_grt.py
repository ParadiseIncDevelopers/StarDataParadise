"""StarDataParadise product tools, docked in the current Classic connection."""
import json
from pathlib import Path
from queue import Queue, Empty
from threading import Thread
from types import SimpleNamespace
import uuid

import grt
import mforms
from wb import DefineModule, wbinputs

from sdp_tools_backend import ManagementSession, Rows, bind, history_sql, identifier, integer, literal, verify_archive

ModuleInfo = DefineModule(name='SdpTools', author='StarDataParadise', version='1.0')
_open_views = []


def box(horizontal=False):
    result = mforms.newBox(horizontal)
    result.set_spacing(8)
    return result


def button(text, callback):
    result = mforms.newButton()
    result.set_text(text)
    result.add_clicked_callback(callback)
    return result


def text(value='', readonly=False):
    result = mforms.newTextBox(mforms.BothScrollBars)
    result.set_value(value)
    result.set_monospaced(True)
    result.set_read_only(readonly)
    return result


def pretty(value):
    return json.dumps(value, ensure_ascii=False, indent=2, default=str)


class Review(mforms.Form):
    def __init__(self, title, target, details):
        super().__init__(mforms.Form.main_form(), mforms.FormDialogFrame)
        self.set_title(title)
        content = box()
        content.set_padding(12)
        content.add(mforms.newLabel(target), False, True)
        content.add(text(details, True), True, True)
        actions = box(True)
        self.cancel = button('Vazgeç', lambda: None)
        self.apply = button('Uygula', lambda: None)
        actions.add_end(self.apply, False, False)
        actions.add_end(self.cancel, False, False)
        content.add(actions, False, True)
        self.set_content(content)
        self.set_size(720, 460)

    def accepted(self):
        self.center()
        return self.run_modal(self.apply, self.cancel)


class ToolPage(mforms.Box):
    title = ''
    description = ''

    def __init__(self, owner):
        super().__init__(False)
        self.owner, self.editor = owner, owner.editor
        self.session = ManagementSession(self.editor)
        self.set_padding(12)
        self.set_spacing(8)
        heading = mforms.newLabel(self.title)
        heading.set_style(mforms.BoldStyle)
        self.add(heading, False, True)
        self.add(mforms.newLabel(self.description), False, True)
        self.fields = {}
        self.form = mforms.newTable()
        self.form.set_column_count(3)
        self.form.set_row_count(0)
        self.form.set_row_spacing(6)
        self.form.set_column_spacing(8)
        self.row = 0
        self.add(self.form, False, True)
        self.actions = box(True)
        self.add(self.actions, False, True)
        self.result_tabs = mforms.newTabView(mforms.TabViewClassic)
        self.grid_box = box()
        self.grid = None
        self.details = text('', True)
        self.result_tabs.add_page(self.grid_box, 'Sonuçlar')
        self.result_tabs.add_page(self.details, 'Ayrıntı')
        self.add(self.result_tabs, True, True)
        self.status = mforms.newLabel('İşlem seçin.')
        self.add(self.status, False, True)
        self.current_rows = []

    def field(self, key, label, value='', choices=None, browse=False, directory=False):
        self.form.set_row_count(self.row + 1)
        if choices is None:
            control = mforms.newTextEntry()
            control.set_value(str(value))
        else:
            control = mforms.newSelector(mforms.SelectorCombobox)
            control.add_items(choices)
            control.set_value(str(value or choices[0]))
        control.set_name(label)
        self.fields[key] = control
        self.form.add(mforms.newLabel(label), 0, 1, self.row, self.row + 1, mforms.HFillFlag)
        self.form.add(control, 1, 2, self.row, self.row + 1, mforms.HFillFlag | mforms.HExpandFlag)
        if browse:
            self.form.add(button('Gözat…', lambda: self.choose(control, directory)), 2, 3, self.row, self.row + 1, 0)
        self.row += 1
        return control

    def choose(self, control, directory):
        chooser = mforms.newFileChooser(mforms.Form.main_form(), mforms.OpenDirectory if directory else mforms.OpenFile)
        chooser.set_title('Klasör seç' if directory else 'Dosya seç')
        if chooser.run_modal():
            control.set_value(chooser.get_path())

    def value(self, key):
        return self.fields[key].get_string_value().strip()

    def action(self, caption, callback):
        result = button(caption, lambda: self.guard(callback))
        self.actions.add(result, False, False)
        return result

    def guard(self, callback):
        try:
            callback()
        except Exception as error:
            self.status.set_text(str(error))
            self.details.set_value(str(error))
            self.result_tabs.set_active_tab(1)

    def show_json(self, value):
        self.details.set_value(pretty(value))
        self.result_tabs.set_active_tab(1)
        self.status.set_text('İşlem tamamlandı.')

    def show_rows(self, result):
        self.current_rows = result.rows
        if self.grid is not None:
            self.grid_box.remove(self.grid)
        self.grid = mforms.newTreeView(mforms.TreeFlatList | mforms.TreeAltRowColors)
        for column in result.columns:
            self.grid.add_column(mforms.StringColumnType, column, 150, False)
        if not result.columns:
            self.grid.add_column(mforms.StringColumnType, 'Sonuç', 200, False)
        self.grid.end_columns()
        for index, values in enumerate(result.rows):
            node = self.grid.add_node()
            node.set_tag(str(index))
            for column, value in enumerate(values):
                node.set_string(column, 'NULL' if value is None else str(value))
        self.grid.add_changed_callback(self.selection_changed)
        self.grid_box.add(self.grid, True, True)
        self.result_tabs.set_active_tab(0)
        self.details.set_value(pretty([dict(zip(result.columns, row)) for row in result.rows]))
        self.status.set_text('%s kayıt.' % len(result.rows))

    def selection_changed(self):
        node = self.grid.get_selected_node() if self.grid else None
        if node:
            self.details.set_value(pretty(self.current_rows[int(node.get_tag())]))

    def selected(self):
        node = self.grid.get_selected_node() if self.grid else None
        if not node:
            raise ValueError('Önce sonuç tablosundan bir kayıt seçin.')
        return self.current_rows[int(node.get_tag())]

    def query(self, sql):
        self.show_rows(self.session.run_sql(sql))

    def open_sql(self, sql):
        query = self.editor.addQueryEditor()
        query.replaceContents(sql)
        self.status.set_text('SQL yeni sorgu sekmesinde incelemeye açıldı.')

    def review(self, caption, details, action):
        review = Review(caption, 'Bağlantı: ' + self.editor.connection.name, details)
        if review.accepted():
            self.session.require_autocommit()
            self.show_json(action())


class HistoryPage(ToolPage):
    title = 'Veri geçmişi'
    description = 'Kurulu History günlüğü - en fazla 256 kayıt - sabit bitiş revizyonuyla sayfalama'

    def __init__(self, owner):
        super().__init__(owner)
        self.field('journal', 'Günlük şeması')
        self.field('mode', 'Görünüm', 'Olaylar', ['Olaylar', 'Satır imajı', 'Fark'])
        self.field('start', 'Başlangıç revizyonu')
        self.field('end', 'Bitiş / hedef revizyon')
        self.field('row', 'Stable satır kimliği')
        self.field('offset', 'Kayıt başlangıcı', '0')
        self.field('limit', 'Sayfa boyutu', '32')
        self.plan = None
        self.action('Günlükleri bul', self.discover)
        self.action('Oku', self.read)
        self.action('SQL’i aç', lambda: self.open_sql(self.statement()))
        self.action('Geri yükleme önizlemesi', self.preview)
        self.apply = self.action('Uygulama SQL’ini incele', self.apply_plan)
        self.apply.set_enabled(False)
        for key in ('journal', 'end', 'row'):
            self.fields[key].add_changed_callback(self.invalidate_plan)

    def invalidate_plan(self):
        self.plan = None
        self.apply.set_enabled(False)

    def discover(self):
        self.query("SELECT ROUTINE_SCHEMA AS journal FROM information_schema.ROUTINES WHERE ROUTINE_NAME='history_log' ORDER BY ROUTINE_SCHEMA")
        if len(self.current_rows) == 1:
            self.fields['journal'].set_value(self.current_rows[0][0])

    def statement(self):
        return history_sql(self.value('journal'), {'Olaylar':'log','Satır imajı':'read','Fark':'diff'}[self.value('mode')],
                           self.value('start'), self.value('end'), self.value('row'), self.value('offset'), self.value('limit'))

    def read(self):
        self.query(self.statement())

    def preview(self):
        self.invalidate_plan()
        journal = self.value('journal')
        revision = integer(self.value('end'))
        row = integer(self.value('row'), minimum=1)
        self.session.require_autocommit()
        result = self.session.run_sql('SELECT %s.history_restore_preview(%s, %s)' % (identifier(journal), revision, row))
        if not result.rows:
            raise ValueError('Sunucu önizleme sonucu döndürmedi.')
        payload = json.loads(result.rows[0][0])
        self.plan = (journal, payload['plan_id'], uuid.uuid4().hex)
        self.apply.set_enabled(True)
        self.show_json(payload)

    def apply_plan(self):
        if self.plan is None:
            raise ValueError('Önce yeni bir geri yükleme önizlemesi alın.')
        journal, plan, token = self.plan
        self.open_sql('-- History geri yükleme: mevcut sorgu işleminin transaction kuralları geçerlidir.\n'
                      '-- Sunucu stale revision, satır kimliği, hesap ve kısıtları yeniden denetler.\n'
                      'SELECT %s.history_restore_apply(%s, %s);\n' % (identifier(journal), literal(plan), literal(token)))


class DecisionPage(ToolPage):
    title = 'Decision kuralları'
    description = 'BIGINT koşulları - tek tablo ve olay - tüm kural setinden gerçek tetikleyici SQL’i'

    def __init__(self, owner):
        super().__init__(owner)
        self.field('file', 'Kaynak dosya', browse=True)
        self.field('schema', 'Varsayılan şema', self.editor.defaultSchema)
        self.source = text()
        self.result_tabs.add_page(self.source, 'Kural kaynağı')
        self.generated = None
        self.action('Dosyayı aç', self.load)
        self.action('SQL üret', self.generate)
        self.inspect = self.action('SQL’i incele', self.inspect_sql)
        self.inspect.set_enabled(False)
        self.action('Betiği kaydet…', self.save)
        self.source.add_changed_callback(self.invalidate)

    def invalidate(self):
        self.generated = None
        self.inspect.set_enabled(False)

    def inspect_sql(self):
        self.assert_generated()
        self.open_sql(self.generated)

    def assert_generated(self):
        if not self.generated or self.generated_key != (self.source.get_string_value(), self.value('schema')):
            self.invalidate()
            raise ValueError('Önce güncel kural kaynağından SQL üretin.')

    def load(self):
        self.source.set_value(Path(self.value('file')).read_text(encoding='utf-8-sig'))
        self.generated = None
        self.inspect.set_enabled(False)
        self.result_tabs.set_active_tab(2)

    def generate(self):
        import sdp_compat_decision as provider
        self.generated = None
        self.inspect.set_enabled(False)
        rules = provider.parse_rules(self.source.get_string_value(), self.value('schema') or None)
        self.generated = provider.provision(rules)
        self.generated_key = (self.source.get_string_value(), self.value('schema'))
        self.inspect.set_enabled(True)
        self.details.set_value(self.generated)
        self.show_rows(Rows([(r.name, r.schema+'.'+r.table, r.event, r.priority, r.effect) for r in rules],
                            ['Kural', 'Tablo', 'Olay', 'Öncelik', 'Karar']))
        self.details.set_value(self.generated)

    def save(self):
        self.assert_generated()
        chooser = mforms.newFileChooser(mforms.Form.main_form(), mforms.SaveFile)
        chooser.set_title('Üretilen SQL’i kaydet')
        chooser.set_extensions('SQL (*.sql)|*.sql', 'sql')
        if chooser.run_modal():
            Path(chooser.get_path()).write_text(self.generated, encoding='utf-8')
            self.status.set_text('Betiğin tamamı UTF-8 olarak kaydedildi.')


class ModelsPage(ToolPage):
    title = 'CPU modelleri'
    description = 'Classic SQL kayıtları ve hesap bazlı CPU runner sürümleri'

    def __init__(self, owner):
        super().__init__(owner)
        self.field('provider', 'Sağlayıcı', 'Classic SQL', ['Classic SQL', 'CPU runner'])
        self.field('registry', 'Kayıt şeması', 'sdp_models')
        self.field('name', 'Model adı')
        self.field('algorithm', 'Algoritma', 'linear', ['linear','logistic','tree','forest','kmeans','dbscan','pca','mlp'])
        self.field('version', 'Sürüm', 'active')
        self.field('worker', 'CPU runner', browse=True)
        self.request = text('{}')
        self.result_tabs.add_page(self.request, 'Eğitim / tahmin JSON')
        self.action('Yenile', self.refresh)
        self.create_button = self.action('Model oluştur', self.create)
        self.action('Eğit', self.train)
        self.action('Tahmin', self.predict)
        self.activate_button = self.action('Etkinleştir', lambda: self.lifecycle(False))
        self.rollback_button = self.action('Önceki sürüme dön', lambda: self.lifecycle(True))
        self.action('Kurulumu denetle', self.preflight)
        self.fields['provider'].add_changed_callback(self.provider_changed)
        self.provider_changed()

    def provider_changed(self):
        classic = self.is_classic()
        self.create_button.set_enabled(classic)
        self.activate_button.set_enabled(not classic)
        self.rollback_button.set_enabled(not classic)
        self.fields['registry'].set_enabled(classic)
        self.fields['worker'].set_enabled(not classic)

    def is_classic(self):
        return self.value('provider') == 'Classic SQL'

    def refresh(self):
        if self.is_classic():
            schema = identifier(self.value('registry'))
            self.query('SELECT d.name, d.algorithm, v.version, v.sha256 FROM %s.sdp_model_definitions d '
                       'LEFT JOIN %s.sdp_model_versions v USING(name) ORDER BY d.name, v.version DESC LIMIT 256' % (schema, schema))
        else:
            self.query('SELECT v.name,v.version,v.algorithm,v.sha256,IF(a.version=v.version,1,0) AS active '
                       'FROM sdp_models.model_versions v LEFT JOIN sdp_models.active_models a USING(owner,name) '
                       'WHERE v.owner=CURRENT_USER() ORDER BY v.name,v.version DESC LIMIT 256')

    def preflight(self):
        self.query("SELECT ROUTINE_NAME FROM information_schema.ROUTINES WHERE ROUTINE_SCHEMA='sdp_models' "
                   "AND ROUTINE_NAME IN('create_model','train_model','predict_model') ORDER BY ROUTINE_NAME")

    def payload(self):
        payload = json.loads(self.request.get_string_value())
        if not isinstance(payload, dict):
            raise ValueError('JSON nesnesi gerekli.')
        payload.update(registry=self.value('registry'), name=self.value('name'))
        return payload

    def statement(self, verb, payload):
        # Classic parser requires a quoted JSON string; apostrophes are doubled.
        # Backslashes depend on sql_mode. Use a JSON argument with no backslash
        # escapes by spelling Unicode directly and rejecting literal backslashes.
        encoded = json.dumps(payload, ensure_ascii=False, separators=(',', ':'))
        if '\\' in encoded:
            raise ValueError('Classic JSON SQL için ters eğik çizgi içermeyen değerler kullanın.')
        self.open_sql("%s MODEL USING '%s';\n" % (verb, encoded.replace("'", "''")))

    def create(self):
        if not self.is_classic():
            raise ValueError('CPU runner modeli ilk eğitimde oluşturur. Eğitim JSON’unu doldurun.')
        self.statement('CREATE', dict(registry=self.value('registry'), name=self.value('name'), algorithm=self.value('algorithm')))

    def train(self):
        payload = self.payload()
        payload.setdefault('request_token', uuid.uuid4().hex)
        if self.is_classic():
            self.statement('TRAIN', payload)
            return
        import sdp_compat_cpu as provider
        args = SimpleNamespace(name=self.value('name'), worker=Path(self.value('worker')),
            algorithm=self.value('algorithm'), schema=payload['schema'], table=payload['table'], order_key=payload['order_key'],
            feature=payload['features'], label=payload.get('label'), split_column=payload.get('split_column'),
            seed=payload.get('seed', 42), options=json.dumps(payload.get('options', {})))
        for name in (args.name, args.schema, args.table, args.order_key, *args.feature):
            provider.identifier(name)
        if args.label is not None:
            provider.identifier(args.label)
        if args.split_column is not None:
            provider.identifier(args.split_column)
        def fit():
            self.session.configure_training_types(args.schema, args.table, [*args.feature, *([args.label] if args.label else [])])
            try:
                return provider.train(self.session, args)
            finally:
                self.session.clear_training_types()
        self.review('Eğitimi incele', pretty(payload), fit)

    def predict(self):
        payload = self.payload()
        version = self.value('version')
        if self.is_classic():
            if version != 'active':
                payload['version'] = integer(version, minimum=1, maximum=0xffffffff)
            self.statement('PREDICT', payload)
            return
        import sdp_compat_cpu as provider
        args = SimpleNamespace(name=self.value('name'), worker=Path(self.value('worker')),
            version='active' if version == 'active' else integer(version, minimum=1, maximum=0xffffffff),
            features=json.dumps(payload.get('features', {})))
        self.show_json(provider.predict(self.session, args))

    def lifecycle(self, rollback):
        if self.is_classic():
            raise ValueError('Classic SQL tahmini belirtilen sürümü veya son sürümü kullanır. Etkin sürüm işaretçisi CPU runner sağlayıcısındadır.')
        import sdp_compat_cpu as provider
        name = self.value('name')
        version = integer(self.value('version'), minimum=1, maximum=0xffffffff)
        self.review('Model sürümünü incele', pretty(dict(model=name, version=version, action='rollback' if rollback else 'activate')),
                    lambda: provider.set_active(self.session, name, version, rollback=rollback))


class JobsPage(ToolPage):
    title = 'Compute işleri'
    description = 'Checksum kuyruğu - hesap kapsamı - UTC aralıklı zamanlama - bağımsız worker'

    def __init__(self, owner):
        super().__init__(owner)
        self.field('mode', 'Görünüm', 'Kuyruk', ['Kuyruk', 'Zamanlamalar'])
        self.field('count', 'İterasyon', '1000')
        self.field('timeout', 'Süre sınırı (ms)', '1000')
        self.field('every', 'UTC aralığı (saniye)', '60')
        self.action('Yenile', self.refresh)
        self.action('İş gönder', self.submit)
        self.action('Zamanlama oluştur', self.schedule)
        self.action('İptal iste', self.cancel)
        self.action('Kuyruğu kur / yükselt', self.install)

    def refresh(self):
        if self.value('mode') == 'Kuyruk':
            self.query('SELECT job_id,state,iterations,checksum,error_code,cancel_requested,attempt,events '
                       'FROM sdp_jobs.compute_jobs WHERE owner=CURRENT_USER() ORDER BY sequence DESC LIMIT 256')
        else:
            self.query("SELECT schedule_id,active,interval_seconds,DATE_FORMAT(next_due,'%Y-%m-%dT%H:%i:%s.%fZ') AS next_utc,iterations,timeout_ms "
                       'FROM sdp_jobs.compute_schedules WHERE owner=CURRENT_USER() ORDER BY next_due LIMIT 256')

    def review(self, caption, details, action):
        self.session.require_tls()
        super().review(caption, details, action)

    def parameters(self):
        return integer(self.value('count'), 1, 1_000_000_000), integer(self.value('timeout'), 1, 60000)

    def submit(self):
        import sdp_compat_jobs as provider
        count, timeout = self.parameters()
        token = uuid.uuid4().hex
        self.review('Compute işini incele', pretty(dict(iterations=count, timeout_ms=timeout, request_token=token)),
                    lambda: dict(job_id=provider.submit(self.session, token, count, timeout)))

    def schedule(self):
        import sdp_compat_jobs as provider
        count, timeout = self.parameters()
        args = SimpleNamespace(count=count, timeout_ms=timeout, every_seconds=integer(self.value('every'), 1, 86400), request_token=uuid.uuid4().hex)
        self.review('UTC zamanlamasını incele', pretty(vars(args)),
                    lambda: dict(schedule_id=provider.create_schedule(self.session, args)))

    def cancel(self):
        import sdp_compat_jobs as provider
        key = self.selected()[0]
        if self.value('mode') == 'Kuyruk':
            self.review('İptal isteğini incele', pretty(dict(job_id=key)), lambda: provider.cancel(self.session, key))
        else:
            def stop():
                self.session.run_sql('UPDATE sdp_jobs.compute_schedules SET active=FALSE WHERE owner=CURRENT_USER() AND schedule_id=?', [key])
                return provider.schedule_status(self.session, key)
            self.review('Zamanlamayı durdur', pretty(dict(schedule_id=key)), stop)

    def install(self):
        import sdp_compat_jobs as provider
        def install():
            provider.install(self.session)
            return dict(status='sdp_jobs kuyruğu ve UTC zamanlama tabloları hazır.')
        self.review('Kuyruk kurulumunu incele', 'sdp_jobs şeması ve compute tabloları oluşturulur. Mevcut kuyruk execution sütunları yükseltilir.', install)


class BackupsPage(ToolPage):
    title = 'Doğrulanmış yedekler'
    description = 'Manifest ve SHA-256 doğrulaması - mantıksal ve fiziksel arşivler'

    def __init__(self, owner):
        super().__init__(owner)
        self.field('archive', 'Arşiv klasörü', browse=True, directory=True)
        self.field('target', 'Yeni fiziksel hedef klasörü')
        main_form, main_row = self.form, self.row
        self.form = mforms.newTable()
        self.form.set_column_count(3)
        self.form.set_row_count(0)
        self.form.set_row_spacing(6)
        self.form.set_column_spacing(8)
        self.row = 0
        self.field('mysqld', 'Server çalıştırılabilir dosyası', browse=True)
        self.field('mysql', 'Classic SQL istemcisi', browse=True)
        self.field('basedir', 'Server basedir', browse=True, directory=True)
        self.field('plugins', 'Server plugin dizini', browse=True, directory=True)
        self.field('options', 'İstemci seçenek dosyası', browse=True)
        self.field('ca', 'Kaynak CA dosyası', browse=True)
        self.field('port', 'Yerel kaynak portu', str(self.editor.connection.parameterValues.get('port', 3306)))
        self.field('user', 'Kaynak kullanıcı', self.editor.connection.parameterValues.get('userName', ''))
        runtime_page = box()
        runtime_page.set_padding(8)
        runtime_page.add(mforms.newLabel('Fiziksel işler için gerçek Server ve istemci çalışma zamanını seçin.'), False, True)
        runtime_page.add(self.form, False, True)
        runtime_scroll = mforms.newScrollPanel()
        runtime_scroll.add(runtime_page)
        self.result_tabs.add_page(runtime_scroll, 'Çalışma zamanı')
        self.form, self.row = main_form, main_row
        self.action('Manifesti aç', self.manifest)
        self.action('Yeniden doğrula', self.verify)
        self.action('Fiziksel yedek oluştur', self.create)
        self.action('Fiziksel geri yüklemeyi incele', self.restore)
        self.busy = False
        self.queue = Queue()

    def manifest(self):
        manifest = json.loads((self.archive_path() / 'manifest.json').read_text(encoding='utf-8'))
        self.show_json(dict(status='Manifest okundu; checksum doğrulaması henüz yapılmadı.', manifest=manifest))

    def background(self, action):
        if self.busy:
            raise ValueError('Arşiv işlemi devam ediyor.')
        self.busy = True
        self.status.set_text('Arşiv işleniyor…')
        def work():
            try:
                self.queue.put((True, action()))
            except Exception as error:
                self.queue.put((False, str(error)))
        Thread(target=work, daemon=True).start()
        mforms.Utilities.add_timeout(0.2, self.poll)

    def poll(self):
        try:
            success, value = self.queue.get_nowait()
        except Empty:
            return True
        self.busy = False
        self.show_json(dict(status='Doğrulandı' if success else 'İşlem başarısız', result=value))
        return False

    def verify(self):
        path = self.archive_path()
        self.background(lambda: verify_archive(path))

    def archive_path(self):
        if not self.value('archive'):
            raise ValueError('Önce arşiv klasörünü seçin.')
        path = Path(self.value('archive'))
        if not path.is_absolute():
            raise ValueError('Arşiv klasörünün tam yerel yolunu girin.')
        return path

    def runtime(self):
        import sdp_compat_physical as provider
        options = Path(self.value('options')).resolve(strict=True) if self.value('options') else None
        return provider.Runtime(provider.plain_path(Path(self.value('mysqld')).resolve(strict=True), False),
            provider.plain_path(Path(self.value('mysql')).resolve(strict=True), False),
            provider.local_directory(self.value('basedir')), provider.local_directory(self.value('plugins')),
            self.value('user'), options)

    def create(self):
        import sdp_compat_physical as provider
        archive = self.archive_path()
        try:
            provider.archive_id(archive.name)
        except Exception:
            archive = provider.local_directory(archive) / uuid.uuid4().hex
            self.fields['archive'].set_value(str(archive))
        archive = provider.local_directory(archive.parent) / archive.name
        runtime = self.runtime()
        ca = Path(self.value('ca')).resolve(strict=True)
        port = integer(self.value('port'), 1, 65535)
        review = Review('Fiziksel yedek oluştur', 'Yerel kaynak: 127.0.0.1:%s' % port,
                        pretty(dict(archive=str(archive), source_port=port, source_ca=str(ca), user=runtime.user,
                                    action='Klon, checksum ve bağımsız geçici geri yükleme denetimi')))
        if review.accepted():
            self.background(lambda: provider.create(runtime, archive, port, ca))

    def restore(self):
        import sdp_compat_physical as provider
        archive = self.archive_path()
        target = Path(self.value('target'))
        if not self.value('target'):
            raise ValueError('Yeni hedef klasörünün tam yolunu girin.')
        if not target.is_absolute():
            raise ValueError('Hedef klasörünün tam yerel yolunu girin.')
        runtime = self.runtime()
        review = Review('Fiziksel geri yüklemeyi incele', 'Yeni hedef: ' + str(target),
                        pretty(dict(archive=str(archive), target=str(target),
                                    action='Manifest doğrulama, yeni kimlikle bağımsız probe ve yeni hedefe yayın')))
        if review.accepted():
            self.background(lambda: provider.restore(runtime, archive, target))


class ToolsView(mforms.AppView):
    names = ('Veri geçmişi', 'Decision kuralları', 'CPU modelleri', 'Compute işleri', 'Doğrulanmış yedekler')

    def __init__(self, editor, page=0):
        super().__init__(False, 'StarDataParadise araçları', 'sdp_tools', False)
        self.editor = editor
        self.set_spacing(0)
        self.layout = box(True)
        self.layout.set_spacing(0)
        sidebar = box()
        sidebar.set_padding(8)
        sidebar.set_size(190, -1)
        sidebar.add(mforms.newLabel('Araçlar'), False, True)
        self.tabs = mforms.newTabView(mforms.TabViewTabless)
        self.pages = []
        for index, page_type in enumerate((HistoryPage, DecisionPage, ModelsPage, JobsPage, BackupsPage)):
            item = page_type(self)
            self.pages.append(item)
            self.tabs.add_page(item, self.names[index])
            sidebar.add(button(self.names[index], lambda index=index: self.select(index)), False, True)
        self.layout.add(sidebar, False, True)
        self.layout.add(self.tabs, True, True)
        self.add(self.layout, True, True)
        self.on_close(self.close_tools)
        self.select(page)

    def select(self, index):
        self.tabs.set_active_tab(index)
        self.set_title(self.names[index])

    def close_tools(self):
        if self.pages[4].busy:
            self.pages[4].status.set_text('Arşiv işleminin tamamlanması bekleniyor.')
            return False
        if self in _open_views:
            _open_views.remove(self)
        return True


def open_tools(editor, page=0):
    for view in _open_views:
        if view.editor == editor:
            view.select(page)
            mforms.fromgrt(editor.dockingPoint).select_view(view)
            return 0
    view = ToolsView(editor, page)
    dock = mforms.fromgrt(editor.dockingPoint)
    dock.dock_view(view, '', 0)
    dock.select_view(view)
    _open_views.append(view)
    return 0


@ModuleInfo.plugin('sdp.tools.open', caption='StarDataParadise araçları', input=[wbinputs.currentSQLEditor()], pluginMenu='SQL/Utilities', accessibilityName='StarDataParadise araçları')
@ModuleInfo.export(grt.INT, grt.classes.db_query_Editor)
def openTools(editor):
    return open_tools(editor)


@ModuleInfo.plugin('sdp.tools.history', caption='Veri geçmişi', input=[wbinputs.currentSQLEditor()], accessibilityName='Veri geçmişi')
@ModuleInfo.export(grt.INT, grt.classes.db_query_Editor)
def openHistory(editor):
    return open_tools(editor, 0)


@ModuleInfo.plugin('sdp.tools.decision', caption='Decision kuralları', input=[wbinputs.currentSQLEditor()], accessibilityName='Decision kuralları')
@ModuleInfo.export(grt.INT, grt.classes.db_query_Editor)
def openDecision(editor):
    return open_tools(editor, 1)


@ModuleInfo.plugin('sdp.tools.models', caption='CPU modelleri', input=[wbinputs.currentSQLEditor()], accessibilityName='CPU modelleri')
@ModuleInfo.export(grt.INT, grt.classes.db_query_Editor)
def openModels(editor):
    return open_tools(editor, 2)


@ModuleInfo.plugin('sdp.tools.jobs', caption='Compute işleri', input=[wbinputs.currentSQLEditor()], accessibilityName='Compute işleri')
@ModuleInfo.export(grt.INT, grt.classes.db_query_Editor)
def openJobs(editor):
    return open_tools(editor, 3)


@ModuleInfo.plugin('sdp.tools.backups', caption='Doğrulanmış yedekler', input=[wbinputs.currentSQLEditor()], accessibilityName='Doğrulanmış yedekler')
@ModuleInfo.export(grt.INT, grt.classes.db_query_Editor)
def openBackups(editor):
    return open_tools(editor, 4)
