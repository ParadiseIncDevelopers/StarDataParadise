# Veri, Şema ve yönetim ekranları

20 Kasım 2026. Veri ve Şema ekranları onaylanan görsellere yaklaştırıldı; SQL inceleme, nesne/bağlantı bilgisi, dil paketi, sunucu özeti, performans ve Değişkenler düzeltmeleri çalışan Classic teslim çıktısında doğrulandı. [Tasarım kuralları](KURALLAR.md) geçerlidir. Derlemeler, 66 Python sınaması, 21 derlenmiş kabul kontrolü, İngilizce/Türkçe yeniden açılış, gerçek veri/DDL akışları, QA temizliği ve son paket/görsel denetimleri tamamlandı. Uygulama Türkçe Veri ekranında açık bırakıldı.

Görsel referanslar: [Veri tablosu](gorseller/02-veri-tablosu.png), [şema inceleyicisi](gorseller/07-sema-inceleyicisi.png), [tablo tasarımı](gorseller/08-tablo-tasarimi.png), [sunucu özeti](gorseller/12-sunucu-ozeti.png), [değişkenler](gorseller/15-degiskenler.png), [performans izleme](gorseller/16-performans-izleme.png) ve [SQL inceleme](gorseller/29-sql-degisiklik-incelemesi.png).

## Ortak yerleşim

Başlık, ana sekmeler, uygulama menüsü ve SQL/Veri/Şema/Performans/Yönetim/Araçlar sırası korunur. Düz yüzeyler, ince nötr ayraçlar, nane seçim yüzeyi ve koyu çizgi ikon ailesi kullanılır. İnsan okunur iki metin arasındaki ayırıcı ` - ` olur. Kalın renkli yan kenar veya süslü kıvrımlı panel eklenmez.

Veri ve yönetim alanları gerçek native sonuç, metadata ve komut nesnelerine bağlıdır. Görsellerdeki örnek sayılar uygulamaya sabit ölçüm olarak aktarılmaz.

## Veri - sonuç ve hücre ayrıntısı

Gerçek tabloya bağlı bir sonuçta Veri alanı, gridin yanında aynı sonuç nesnesini kullanan dar bir ayrıntı formu açar. Bölme çizgisi kullanıcı tarafından taşınabilir. Form ilk ihtiyaçta oluşturulur; SQL alanına dönüldüğünde gizlenebilir. Tablo ilişkisi olmayan sabit ifade sonuçları için sahte bir düzenlenebilir tablo formu üretilmez.

Seçim ve kayıt gezinmesi grid ile ortak sonuç nesnesinden gelir. NULL ve boş metin farklı durumlar olarak gösterilir. Salt okunur sonuç, düzenlenebilir sonuçtan ayrılır; formda yapılan yerel değişiklikler sunucuya gönderme onayı yerine geçmez. Mevcut Uygula/Geri eylemleri ve native satır değişikliği modeli korunur.

Kaynaklar: `wb_sql_editor_panel.cpp`, `wb_sql_editor_result_panel.cpp/.h`, `result_form_view.cpp/.h`, `RecordsetView.cs`.

## Şema - tablo düzenleme ve nesne bilgisi

Tablo düzenleyicisinin başlığında şema, tablo adı, karakter kümesi, depolama motoru, karşılaştırma düzeni ve açıklama ilk açılışta görünür. Eski gizli başlık yerine kompakt iki sütunlu alan yerleşimi kullanılır. Sütun listesi ve ayrıntı formu mevcut backend'e bağlıdır; modelin yerine başka bir form verisi oluşturulmaz.

Sütun ekleme ve yukarı/aşağı taşıma komutları mevcut sütun modelini kullanır. Sütun adı, veri türü, varsayılan değer/ifade, açıklama, karakter kümesi ve karşılaştırma alanları korunur. PK, boş olamaz, benzersiz, işaretsiz ve otomatik artış gibi bayraklar bağımsızdır. Üretilmiş sütun ve Virtual/Stored seçenekleri aynı native davranışla çalışır. Kullanıcının yazdığı SQL kimlikleri ve veri türleri çevrilmez.

Şema gezginindeki Nesne bilgisi artık İngilizce Table/Columns başlıklarına ve eski yoğun HTML görünümüne dayanmaz. Nesne türü/adı, sütun sayısı ve varsa PK/FK özeti sunulur. Sütun adları ve türler gerçek metadata'dan gelir. Nesne adları HTML içine yerleştirilmeden kaçırılır; özel karakterli adlar biçimlendirme komutu sayılmaz.

Şema inceleyicisinde tablo seçimi sağda gerçek sütun, indeks ve salt okunur DDL önizlemesini günceller. Tanımı düzenle, seçili gerçek tabloyu native editörde açar. Boş veya çoklu seçimde önceki tablonun ayrıntısı güncel seçim gibi bırakılmaz. Metadata okuma hatası açıklanır; başarısız sorgunun yerine örnek satır gösterilmez. Sorgu sonuç referansları bırakılır.

Kaynaklar: `DbMysqlTableEditor.cs`, `wb_live_schema_tree.cpp`, `wb_sql_editor_tree_controller.cpp`, `plugins/wb.sqlide/sqlide_catalogman_ext.py`, `sqlide_tableman_ext.py`.

## SQL değişikliklerini incele

Tablo tanımı ve satır değişiklikleri aynı native SQL inceleme ailesini kullanır. Hedef bağlantı/nesne özeti, işlem açıklaması ve salt okunur SQL betiği birlikte görünür. Satır değişikliği ile DDL açıklaması farklıdır; DDL'nin kalıcı olabileceği kısa metinle belirtilir. İnceleme sayfasının açılması SQL'in yürütüldüğü anlamına gelmez.

Uygula, mevcut SQL yürütme akışına gider. Geri ve Vazgeç davranışları korunur. Betiği kaydet eylemi görünen SQL'i yerel dosyaya yazar. Desteklenen native sürümlerde algoritma/kilit seçenekleri SQL üreticisine bağlıdır; kullanıcıya çevrilmiş başlıklar gösterilirken SQL token'ları özgün değerleriyle kalır. Uygulama sonucu gerçek başarı/hata kaydından alınır.

Kaynak: `backend/wbpublic/sqlide/sql_script_run_wizard.cpp/.h`. İlk canlı kabulde satır düzenleme için Vazgeç ve Uygula ayrı ayrı doğrulandı. Tablo açıklamasının ALTER önizlemesinden vazgeçilip yerel değişiklik geri alındığında sunucu tanımı değişmedi. Yatay Geri/Vazgeç/Uygula alt sırası çalışan pakette yeniden görüntülendi.

## Dil paketi

Arayüz dili Tercihler/Görünüm alanındaki seçimle `workbench:UILanguage` anahtarında saklanır. Türkçe (`tr`) varsayılandır; İngilizce (`en`) seçilebilir. Değişiklik uygulamanın yeniden açılmasını gerektirir. Başlangıçta kaydedilen dil, native/Python/WinForms kontrolleri oluşturulmadan okunur.

Ortak JSON paketleri `classic-workbench/res/locales/` altındadır: `tr.json`, `tr-workspace.json`, `tr-data-schema.json`, `tr-admin.json`. Native `_()`, Python `workbench.sdp_i18n.tr` ve WinForms `SdpLanguage.Translate/Apply` aynı kaynak/çeviri eşlemelerini kullanır. Paketler çalışma zamanının `locales/` dizinine sahnelenir.

Üçüncü kullanıcı görselindeki sonraki/önceki ana sekme komutları bu pakete bağlanır. Menü başlığı, düğme, araç ipucu, form etiketi ve sütun başlığı çevrilebilir; SQL editörü içeriği, grid hücresi, dosya yolu, kullanıcı nesne adı ve sunucunun teknik değerleri çeviri girdisi yapılmaz. Paket içinde bulunmayan bir arayüz ifadesi kaynak metinle kalır.

Değişken açıklamalarında pakete alınmış çeviri varsa kullanılır. Henüz ayrıntılı çevirisi bulunmayan eski upstream açıklama, Türkçede kısa ve doğru yerel ayar/sayaç açıklamasıyla gösterilir. İngilizce kipte özgün ayrıntılı açıklama korunur. Bu davranış bütün üçüncü taraf yardım veya sunucu hata metinlerinin çevrildiği iddiası değildir.

## Yönetim ve SDP SQL ölçümleri

Yönetim gezinmesindeki girişler ortak koyu çizgi ikonlarını kullanır. Sunucu özeti, oturumlar, kullanıcılar/yetkiler, değişkenler, SQL içe/dışa aktarma, hizmet, günlükler, yapılandırma, canlı izleme ve ölçüm ayarlarının mevcut native hedefleri korunur. Data Import/Restore, Startup/Shutdown ve Dashboard başlıkları da dil paketine bağlanır. Dış yönetim gezginini tekrarlayan ikinci iç düğme şeridi kaldırıldı; sunucu özellikleri kesilen iki tablo yerine tek etiket/değer tablosuna taşındı. SDP eylemleri ve Yenile tam metinle görüntülendi; Türkçe profil/port/TLS bilgi alanı da son pakette doğrulandı.

Sunucu özetindeki MySQL logosu ve eski dokulu ölçüm yüzeyleri kaldırılır. Profil uç noktası ile sunucunun kendi bildirdiği port ayrı alanlardır. Sürüm, platform, yapılandırma yolu ve çalışma başlangıcı gerçek kaynaklarından gelir. İşletim sistemi izleyicisinden doğrulanmamış CPU oranı gösterilmez.

| Alan | Gerçek kaynak ve yorum |
| --- | --- |
| SDP SQL komutları | `STARDATAPARADISE_CAPABILITIES()` içindeki `commands`. Model kayıt tablolarının kurulumu ayrıca denetlenir. |
| Uyumluluk kipi | Yönetim bağlantısının `@@SESSION.sdp_compatibility_mode` değeri. |
| Model tanımları | Varsayılan `sdp_models.sdp_model_definitions` tablosundaki görünür kayıtların sayısı. |
| Eğitilmiş model sürümleri | Varsayılan `sdp_models.sdp_model_versions` tablosundaki görünür kayıtların sayısı. |
| Bekleyen ve çalışan işler | `sdp_jobs.compute_jobs` içindeki `queued`/`running` kayıtlarının sayısı. |
| Geçmiş yordamları | Erişilebilen `history_log`, `history_read`, `history_diff` yordam adlarının sayısı. Bu, günlük kaydı sayısı değildir. |
| Yönetim oturumu TLS | O yönetim bağlantısının `SHOW SESSION STATUS LIKE 'Ssl_cipher'` yanıtı. Sunucunun genel TLS desteği veya başka bir SQL oturumunun şifre takımı değildir. |

Kurulu olmayan araç, yetki yetersizliği, alınamayan değer ve gerçek sıfır birbirinden ayrılır. Bu görünüm araç kurmaz, kuyruk oluşturmaz ve model eğitmez. SDP yetenek/kayıt değerleri Yenile ile okunur. Bağlı/etkin oturum, işlenen SQL, alınan/gönderilen bayt ve InnoDB tampon kullanımı mevcut sunucu durum örneklerinden yenilenir.

Performans görünümünde SDP SQL/oturum özeti ve ayrı Ağ/depolama görünümü bulunur. Mevcut gerçek ağ, SQL ve InnoDB grafikleri korunur; eski marka başlık resimleri ve dekoratif bitmap okları çizilmez. Grafik başlıkları/araç ipuçları dil paketine bağlıdır. Ardışık örnekten hız hesabı sıfır değer/zaman damgasını işler; sayaç sıfırlanması negatif hız, eksik sayaç sahte sıfır üretmez. Ağ/depolama görünümü mevcut native kaydırılabilir çizim alanını kullanır.

Kaynaklar: `wb_admin_utils.py`, `wb_admin_server_status.py`, `wb_admin_monitor.py`, `wb_admin_performance_dashboard.py`, `wb_admin_grt.py`, `wb_admin_main.py`.

## Değişkenler düzeltmesi

İlk açılışta görülen `TreeView.select_node() takes 2 positional arguments but 3 were given` hatası, Python bağında bulunmayan ek boolean argümandan kaynaklanıyordu. Başlangıç seçimi tek düğüm argümanıyla yapılır. Kullanıcının mevcut seçimi varsa korunur. Tümü, arama, Durum sayaçları ve Sistem değişkenleri etiketleri dil paketine bağlıdır; kategori etiketleri çevrilirken dahili grup anahtarları korunur. İlk yeni uygulamada sayfa gerçek satırlarla açıldı ve hata tekrarlanmadı.

Son görsel düzeltmede sayaç açıklaması ile sistem ayarı açıklaması ayrıldı. Tümünü kopyala ve Seçimi kopyala kısa etiketleri, tam amaç araç ipuçları ve en az 90 px Yenile genişliği kullanılır. Kategori/sonuç satırları 26 px; arama, kategori, sonuç ve eylemler Segoe UI 9.75 kullanır. Yeni Türkçe canlı turda Durum sayaçları ve Sistem değişkenleri gerçek satırlarla, kısa eylem metinleriyle ve istisnasız yeniden açıldı.

Bu etap görünüm ve açılış hatasını kapsar. Canlı kabul için sistem değişkeni değiştirilmesi veya RESET PERSIST çalıştırılması gerekmiyor; mevcut yetki ve yazma davranışları bu ekranlardan otomatik tetiklenmez.

## Doğrulama durumu

| Durum | Kontrol |
| --- | --- |
| PASS | 66 Python davranış sınaması: 12 ürün aracı, 5 çalışma alanı işlemi, 10 şema inceleyicisi, 12 dil paketi, 12 yönetim ölçümü, 7 sorgu planı ve 8 şema bağlam menüsü/dil kabulü. Yedi grubun dönüş kodu sıfırdır. `data-schema-behavior-tests.json`. |
| PASS | 21 derlenmiş kabul kontrolü: iki C++ grubunda 5+5 ve C# kaydırma grubunda 11 kontrol. Bunlar Python sayımından ayrıdır. |
| PASS | İlk 18 native hedefin tam derlemesi, sonraki hedefli turlar, altı hedefli dil derlemesi ve son WBControls/wbprivate iki hedefli teslim derlemesi tamamlandı. Kapalı QA profiliyle gerçek soğuk açılış yapıldı. |
| PASS | Teslim çalışma zamanının yedi kontrolü: 340 kanonik/native/eşleme manifesti hash'i, 28 Python/XML, dört katalog, 219 ikon/lisans kaynağı, 28 zorunlu dosya ve 18 x64 PE eşleşti. `data-schema-runtime-validation.json`. |
| PASS | Kaynak doğrulamasının dokuz kontrolü: 36 Python AST, beş XML, 20 Roslyn C# sözdizimi, 1.299 katalog girdisi/biçim argümanı, UI katalog kapsamı, kurulum/sahneleme, ikonlar, metin ayraçları ve whitespace. `data-schema-source-validation.json`. |
| PASS | İzole QA tablosunda SQL önizlemesinden Vazgeç sonrası tutar 20.00 kaldı; Uygula sonrası bağımsız native okuma 24.75 döndürdü. ALTER açıklama taslağından Vazgeç/Geri al sonrası SHOW CREATE TABLE ve veri değişmedi. Native after-cancel/after-apply/after-ddl-cancel kayıtları. |
| PASS | Veri sağ formu aynı sonuç/kayda bağlıdır. Veri/SQL dönüşü ve etkin belgeyi koruyan yeni gezinme geçti. Şema inceleyicisinde gerçek sütun/indeks/DDL, Tanımı düzenle ile native C# editörü, tablo/PK alanları ve önizlemeden Vazgeç/Geri al doğrulandı. |
| PASS | Threads araması altı gerçek durum satırı döndürdü; Sistem değişkenleri istisnasız açıldı. Sayaç/ayar açıklamaları ayrıldı; kopyalama ve Yenile tam metinle görüntülendi; eski select_node hatası tekrarlanmadı. |
| PASS | Sunucu özeti ve gerçek SDP yetenek/TLS bilgisi okundu. SDP ve ağ/depolama görünümleri açıldı. Türkçe bağlantı bilgi alanında gerçek profil, 3197 uç noktası ve TLS gösterildi. Windows disk alanı etiketi son soğuk açılışta Türkçe görüldü; ham sayılar/birimler korunur. |
| PASS | İngilizce açılışta loaded_ui_language=en ve Table/Columns; Türkçe yeniden açılışta loaded_ui_language=tr ve Tablo/Sütunlar doğrulandı. Ana sayfa/ortak alan görüntüleri dil paketini doğrular; Edit/Server başlıkları doğru kaldı. Native language-en/language-tr kayıtları. |
| PASS | Son soğuk açılışta tam sonuç başlığı, Form Önceki/İleri, Türkçe SQL dosyaları (*.sql) kaydetme türü ve sonuç sütun türü/ölçüm/plan başlıkları görüntülendi. Kaydet iletişiminden Vazgeçildi; sql uzantısı ve *.sql deseni değişmedi. |
| PASS | Kabul sorgusu `out/classic-workspace-audit/data-schema-cases/teslim_kabulu_20261120.sql` dosyasına UI ile kaydedildi. Yıldızsız sekmeler görüldükten sonra QA normal kapandı; eski kullanıcı oturumu korundu. |
| PASS | Yalnız owned benzersiz QA şeması native koşucuyla temizlendi; yokluğu doğrulandı. Native cleanup başarılı ve fixture-state cleaned=true. Mevcut kullanıcı şemaları korunur. |
| FIX / PASS | Süresi dolan fixture CA/sunucu sertifikaları aynı özel anahtarlarla 30 günlük yenilendi. localhost DNS ve 127.0.0.1 IP SAN doğrulamaları ile strict TLS resume geçti. Yalnız owned backend/gateway açıldı; mevcut 3306 sunucusuna dokunulmadı. |
| PASS | Son uygulama Türkçe, test - 127.0.0.1:3197 bağlantısında açık bırakıldı. `new_db_for_ai.ai_exam_demo` sekiz gerçek satır gösteriyor; grid ve sağ form aynı seçili kaydı id=8, study_hours=8, exam_score=80 olarak sunuyor. Nesne bilgisinde üç sütun, PK=1/FK=0 ve ortak ikonlar görüntülendi. |
| PASS | Canlı kabul raporundaki altı veri/TLS kontrolü ve on teslim UI kontrolü geçti. `data-schema-live-acceptance.json`. 21 kalıcı JPEG'nin tamamı 1920×1020, format/decode/hash denetimi başarılı. Son paket denetiminin 18/18 kontrolü ve 414 yerel bağlantı doğrulaması geçti. |

İlk kabulün veri yazmaları yalnız bu iş için oluşturulan benzersiz `sdp_ui_qa_…` şeması içindedir; mevcut kullanıcı tablolarına yazma yapılmadı. Önceki `out/classic-workspace-fidelity-ui` kullanıcı uygulaması ve ayarları korunur. İlk QA ve işe ait boş uygulamalar normal kapatıldı; kaydedilmemiş sorgu içeren eski açık kabul oturumu ve kullanıcı uygulamaları korunur. Yeni teslim çıktısı bu nedenle ayrı `out/classic-data-schema-delivery` dizinine yönelir. MSI kur/kaldır ve tam DPI/yüksek kontrast/ekran okuyucu matrisi bu kayıtla tamamlandı sayılmaz.

İlk derleme denemesinde base `makePath` bildirimi için eksik başlık, sonraki denemede C++/CLI hata penceresi çağrısının `const char*`/`System::String^` sınırı düzeltildi. Tekrar tam derleme geçti. Günlükler `data-schema-first-build-base-error.log` ve `data-schema-second-build-managed-error.log` dosyalarındadır. Son alt düğme yerleşimi derlemesinde `AutoSizeMode::GrowAndShrink`, Form özelliğiyle çakıştı; enum tam adlandırıldı ve üç hedefli tekrar derleme geçti. Önceki başarısız deneme günlüğü `data-schema-footer-build-enum-error.log`. İlk yerel fixture koşucusunun sonuç sütun sayısı okuması gerçek `columns` koleksiyonuna uyarlandı; ardından bağımsız native okuma kontrolleri geçti.

Yeniden sahnelemede açık görüntü dosyası kilidi görüldü; CopyFlat aynı SHA-256 içeriği yeniden yazmadan atlayacak biçimde düzeltildi. Kaynak sahneleme tekrarı ve ardından kapalı QA profiliyle yapılan derleme geçti. Kaydedilmemiş sorgu içeren eski kullanıcı oturumunu zorla kapatmak gerekmedi.

Yerel ortak dinleyicide dört eski ve dört QA bağlantısının sekiz yuva sınırını doldurması tanılandı. Sunucu veya dinleyici yapılandırması değiştirilmedi; yalnız işe ait uygulamalar normal kapatıldı. Kaydedilmiş ortak uç nokta ile doğrudan arka uç adresi aynı bağlantı sayılmadı; doğrudan port için önbelleğin eski doğrulamayı kabul etmemesi doğru davranış olarak korundu.

Derleme komutu depo kökünden:

```powershell
pwsh -NoProfile -File packaging/classic-workbench/Build-Workspace.ps1 -Output out/classic-data-schema-delivery -RuntimeSeed out/classic-data-schema-final
```

Bu etabın gerekli derleme ve canlı kabul işleri tamamlanmıştır. Ham raporlar ignore edilen `out/classic-workspace-audit/` altındadır; kalıcı görseller aşağıdadır.


<a id="canli-kabul-goruntuleri"></a>
## Son canlı görüntüler

21 görüntü çalışan uygulamadan alınmış ve son set olarak decode/hash denetimini geçmiştir. Tasarım görsellerindeki örnek değerlerden ayrı, gerçek native kabul kanıtlarıdır.

| Görüntü | Doğrulanan durum |
| --- | --- |
| [Veri satır ayrıntısı](uygulama/veri-sema-yonetim/01-veri-satir-ayrintisi.jpg) | Gerçek sonuç ve aynı kayda bağlı sağ form |
| [SQL önizlemesi](uygulama/veri-sema-yonetim/02-sql-onizleme.jpg) | Gerçek betik ve yatay alt eylemler |
| [Tablo düzenleme](uygulama/veri-sema-yonetim/03-tablo-duzenleme.jpg) | Native sütun ve PK alanları |
| [Sunucu özeti](uygulama/veri-sema-yonetim/04-sunucu-ozeti.jpg) | Tek gezgin, gerçek sunucu bilgisi ve Türkçe disk alanı |
| [Durum sayaçları](uygulama/veri-sema-yonetim/05-durum-sayaclari.jpg) | Gerçek durum satırları |
| [Sistem değişkenleri](uygulama/veri-sema-yonetim/06-sistem-degiskenleri.jpg) | İstisnasız sistem görünümü |
| [SDP performans özeti](uygulama/veri-sema-yonetim/07-performans-sdp.jpg) | Gerçek SDP SQL ve oturum bilgisi |
| [Ağ ve depolama](uygulama/veri-sema-yonetim/08-performans-ag-depolama.jpg) | Gerçek ağ/SQL/InnoDB grafikleri |
| [Şema sütunları](uygulama/veri-sema-yonetim/09-sema-sutunlar.jpg) | Dört gerçek QA sütunu |
| [Şema DDL](uygulama/veri-sema-yonetim/10-sema-ddl.jpg) | Salt okunur sunucu tanımı |
| [İngilizce ana sayfa](uygulama/veri-sema-yonetim/11-ingilizce-ana-sayfa.jpg) | Dil paketli ana sayfa metinleri |
| [Türkçe ana sayfa](uygulama/veri-sema-yonetim/12-turkce-ana-sayfa.jpg) | Türkçe yeniden açılış |
| [Son çalışan Veri ekranı](uygulama/veri-sema-yonetim/14-son-uygulama.jpg) | Sekiz gerçek satır ve aynı seçili sekizinci kayda bağlı form |
| [İngilizce çalışma alanı](uygulama/veri-sema-yonetim/15-ingilizce-calisma-alani.jpg) | Doğru Edit/Server ve ortak başlıklar |
| [Şema indeksleri](uygulama/veri-sema-yonetim/16-sema-indeksler.jpg) | Gerçek indeks metadata bilgisi |
| [Bağlantı bilgisi](uygulama/veri-sema-yonetim/17-baglanti-bilgisi.jpg) | Türkçe gerçek profil/port/TLS |
| [Sonuç sütun türleri](uygulama/veri-sema-yonetim/19-sonuc-sutun-turleri.jpg) | Gerçek sonuç metadata bilgisi |
| [Sonuç ölçümleri](uygulama/veri-sema-yonetim/20-sonuc-sorgu-olcumleri.jpg) | Gerçek ölçüm ve toplanmayan ayrıntı açıklaması |
| [Sonuç planı](uygulama/veri-sema-yonetim/21-sonuc-sorgu-plani.jpg) | Gerçek plan ağacı; Tablo/JSON girişleri |
| [Sonuç Formu](uygulama/veri-sema-yonetim/22-sonuc-form.jpg) | Soğuk açılışta Önceki/İleri |
| [Türkçe SQL kaydet](uygulama/veri-sema-yonetim/23-sql-kaydet-turkce.jpg) | SQL dosyaları (*.sql) türü |
