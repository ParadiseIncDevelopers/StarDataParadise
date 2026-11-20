# Classic çalışma alanı uygulama kaydı

20 Kasım 2026. [Tasarımlar](README.md) - [Yerleşim kuralları](KURALLAR.md) - [Komut envanteri](KOMUTLAR.md) - [Ürün araçları](ARACLAR.md) - [Veri/Şema ve yönetim kabulü](VERI-SEMA-YONETIM.md).

Son kullanıcı düzeltmesi bütün ekranlar için başlık → ana sekmeler → uygulama menüsü → bağlantı alanları sırasıdır. Kalın renkli yan şeritler ve belirgin kıvrımlı kutu çerçeveleri kullanılmaz. Kesin ölçüler kurallar belgesinden alınır.

Bu kayıt çalışan kaynakları ve 19-20 Kasım native uygulama etaplarını açıklar. Galerideki 41 görsel tasarım referansıdır; örnek veriler ve tasarım aşamasındaki canlı inceleme, aşağıdaki uygulama kabulüyle ayrı tutulur. İnsan tarafından okunan iki bilgi parçası ` - ` ile ayrılır; SQL nesne adları, IP adresleri ve sürüm numaralarındaki anlamlı noktalar korunur.

Güncel teslim çıktısı `out/classic-data-schema-delivery/StarDataParadise.exe` olur. [Veri/Şema ve yönetim kabulü](VERI-SEMA-YONETIM.md), tamamlanan derlemeleri, 66 Python sınamasını, 21 derlenmiş kabul kontrolünü, Türkçe/İngilizce yeniden açılışı, satır Uygula/Vazgeç ve ALTER iptalini, temizlenen QA şemasını ve 21 son native görüntüyü içerir. [Açık bırakılan son Veri ekranı](uygulama/veri-sema-yonetim/14-son-uygulama.jpg), gerçek sekiz satırı ve aynı kayda bağlı grid/form görünümünü gösterir.

Önceki `out/classic-workspace-fidelity-ui` çalışma zamanı korunur. [Görsele yakınlık ve sonuç alt sekmeleri kabulü](#gorsel-uyum), o etabın tamamlanan sonuç sekmesi kontrollerini ve kalıcı native görüntülerini içerir.

## Ortak kabuk ve bağlantı beklemesi

Windows başlık çubuğunun altında ana belge şeridi, uygulama menüsü ve etkin bağlantının alanları bulunur. `MainForm`, `FlatTabControl` ve ortak mforms kontrolleri düz yüzey, ince nötr ayraç, Segoe UI ve kompakt ölçüler kullanır. Ana sayfa sekmesi sabittir. Bağlantı sekmesinin avatarı kayıtlı `sdpHomeAvatar` değerinden alınır. Profil avatarlarının eklenmesinde bulunan ImageList anahtar arama hatası, ayrı kimlik/indeks sözlüğüyle giderildi; başarılı bağlantı ve sorguyla tekrar doğrulandı.

Sol gezgin başlangıçta Şemalar alanını gösterir. Sağ yardım paneli başlangıçta kapalıdır. Alanlar arasında geçiş mevcut panel durumunu korur. Yeni Veri/Şema etabında Yönetim için dış yönetim gezgini kullanılır; aynı komutları tekrarlayan ikinci iç düğme şeridi kaldırılmıştır. Diğer native başlıklar ve düğmeler de ortak düz stilleri kullanır. Sihirbazların adım listesi ve gezinme düğmeleri aynı kompakt aileye taşındı.

Ana sayfadan bağlantı açılırken seçili profil satırı **Bağlanıyor… / Vazgeç** durumunu gösterir. Native bağlantı işçisi, TLS/SSH/parola akışı ve iptal çağrıları korunur. UI bekleme döngüsünde tekrarlanan açma, diğer ana sayfa eylemleri ve pencere kapanışı denetlenir. İptal talebinden sonra geç tamamlanan bağlantı sonucu serbest bırakılır. İptal işbirliklidir; bağlayıcının mevcut zaman aşımına kadar beklemesi mümkündür. Ana sayfa dışındaki genel bekleme penceresi de eski logolu yüzey yerine kompakt düz görünüm kullanır.

İzole kabul oturumları `--configdir` ile açılır; günlük dizini ve tek-instance kimliği de bu dizine bağlıdır. Yeni çıktı, açık eski kullanıcı uygulamasını ve onun kaydedilmemiş tamponlarını kapatmadan çalıştırıldı.

## Tercihler, ürün bilgisi ve hata penceresi

Tercihler sayfasında gerçek kategori adlarını süzen arama, kompakt alanlar ve mevcut ayar işleyicileri vardır. Ürün bilgisi penceresi **Ürün bilgisi / Bileşen lisansları / Tanılama** sekmelerini, gerçek sürüm/derleme ve çalışma zamanı yollarını gösterir; sürüm bilgisini kopyalar. Upstream ve bağımlılık lisans bildirimleri korunur.

Beklenmeyen hata penceresinin başlığı **StarDataParadise - Hata** olur. Ürün yıldızı, kısa Türkçe açıklama, açılıp kapanan salt okunur teknik ayrıntı, **Ayrıntıları kopyala** ve **Kapat** eylemleri bulunur. Kopyalama sonucu aynı pencerede gösterilir; Esc kapatır. Eski MySQL Workbench hata resmi ve dış hata raporu yönlendirmesi kaldırıldı. Gerçek derlenmiş `ExceptionDialog` bileşeni ayrı bir test hostunda açılarak ayrıntı, kopyalama ve Esc davranışları kontrol edildi. Aşağıdaki hata görüntülerindeki istisna metni test girdisidir; uygulamanın yeni bir üretim hatası değildir.

| Çalışan uygulama görüntüsü | İçerik |
| --- | --- |
| [Ana sayfa](uygulama/ana-sayfa.jpg) | Dört katmanlı pencere düzeni ve kompakt profil alanı |
| [SQL ve sonuç](uygulama/sql-sonuc.jpg) | Canlı TLS bağlantısında sabit SELECT sonucu ve yeni sonuç sekmeleri |
| [Hata penceresi](uygulama/hata-kapali.jpg) | Kısa açıklama ve kapatma eylemi |
| [Teknik ayrıntı](uygulama/hata-ayrintili.jpg) | Salt okunur açılır ayrıntı alanı |
| [Kopyalama sonucu](uygulama/hata-kopyalandi.jpg) | Gerçek panoya kopyalama sonrası durum metni |

## SQL ve yönetim

`wb_sql_editor_form_ui.cpp` oturum araç çubuğunda SQL, Veri, Şema, Performans, Yönetim ve Araçlar girişlerini oluşturur. Alan değişince ilgili sayfa/işlem seçicisi güncellenir. **Aç** eylemi mevcut komut işleyicisini çağırır. Veri alanının varsayılan eylemi, seçili tablo/görünüm için şema ağacındaki mevcut **Select Rows** işleyicisini çağırır. SQL'e dönüş açık sorgu tamponunu seçer, diğer sayfalar ve bağlantının işlemi korunur. SQL editöründeki Çalıştır, İfade, Plan, Durdur, Commit, Rollback ve otomatik commit aynı native yürütme işlevlerini kullanır. Plan eylemi `SqlEditorForm::explain_current_statement` ve mevcut sonuç/plan görünüm seçimini korur.

Yönetim sayfaları ortak kompakt başlık ve mevcut veri modellerini kullanır. Dokuz yönetim ve üç performans girişinin kendi sayfa seçimi vardır. Eksik yapılandırma veya yükleme hatasında **Profili yapılandır / Yeniden dene** eylemleri sunulur. Değişkenler sayfasının ilk açılışında kategori seçilmediği için boş kalması, veriler yüklendikten sonra Tümü kategorisini seçerek giderildi. Ölçüm ayarlarında gelişmiş Instruments sayfası başlangıçta seçilir. Sunucu uyumsuzluğu veya yetki hatası gerçekleşirse hata yüzeyi gösterilir.

## Nesne, dosya ve bakım yüzeyleri

| Taslak | Uygulanan yüzey ve gerçek işleyici |
| --- | --- |
| 02 - Sonuç görünümleri | `wb_sql_editor_result_panel.cpp`. Sağdaki büyük ikonlu dikey şerit kaldırıldı. Tablo, Form, Sütun türleri, Sorgu ölçümleri ve Sorgu planı aynı native sonuç alanının kompakt yatay metin sekmeleridir. Geometri destekli sonuçta Mekânsal görünüm de bulunur. Mevcut grid/export/düzenleme ve her görünümün veri işleyicileri korunur. Görünümlerin oluşturulması, dock/undock ve kapanış sırasında gelen programatik sekme olaylarıyla yeniden tetiklenmez. |
| 07 - Şema ve tablo inceleyicisi | `sqlide_catalogman_ext.py`, `sqlide_tableman_ext.py`. Ortak kompakt başlık, daraltılan metadata boşlukları, Türkçe görev/sekme etiketleri. Mevcut metadata sorguları, indeks araçları ve native nesne editörleri korunur. |
| 11 - Veride arama | `plugins/db.search/DbSearchFilterPanel.cpp`, `DbSearchPanel.cpp`, `register_plugin.cpp`. Eşleme yöntemi, tablo/toplam sınırı, tüm türler, başlat/durdur ve sonuç sütunları kompakt Türkçe kontrollerle sunulur. Kapsam şema gezgininin gerçek seçimidir. **SQL önizle…** ilk seçim kapsamındaki bir örnek tablo için salt okunur sorgu gösterir. Aynı DBSearch metadata çözümlemesi ve `build_select_query` üreticisi kullanılır; önizlemede tablo verisi SELECT'i çağrılmaz. Çözümleme bir kapsam, bir örnek tablo ve onun sütun/anahtar metadata'sıyla sınırlıdır. Sonraki tabloların LIMIT değerinin kalan toplam sınıra göre hesaplanacağı açıklanır. Gerçek arama mevcut SELECT yürütmesi ve iptal mekanizmasını kullanır. |
| 19 - SQL dışa aktarma | `wb_admin_export.py`, `WbAdminExportTab`, `DumpThread`. Nesne seçimi, yapı/veri kipi, tetikleyici/yordam/olay, tek dosya/klasör seçenekleri ve ilerleme korunur. Seçenek başlıkları ve komutlar kısaltıldı; yol satırları daraltıldı ve açıklamalar sarılır. Gerçek mysqldump işçisi kullanılır. |
| 20 - SQL içe aktarma | `WbAdminImportTab`, mevcut mysql yükleme işçisi. Kaynak dosya/klasör seçimi ve hedef şema alanı korunur. **SQL önizle…** seçili dosyayı veya klasörde işaretli nesnelerin dosyalarını salt okunur olarak açar. Dosya seçicisi önizleme içinde geçiş sağlar. İlk 64 KiB okunur; gösterim tek başına işçi başlatmaz. Betikteki USE/CREATE DATABASE etkisi ve gerçek hedef bağlantı/şema özeti görünür. |
| 28 - CSV/JSON içe aktarma | `sqlide_power_import_wizard.py`. Dosya, yeni/mevcut tablo, kodlama, sütun eşleştirme, veri önizlemesi ve ilerleme aynı native sihirbazda çalışır. Büyük aralıklar azaltıldı, eylem ve alan etiketleri Türkçeleştirildi. Kodlama/dönüşüm motoru değiştirilmedi. |
| 38 - Tablo bakımı | `TableMaintenancePanel`, `review_maintenance_sql`, `do_table_maintenance`. ANALYZE / OPTIMIZE / CHECK / CHECKSUM seçicisi yalnız ilgili seçenekleri gösterir. **SQL’i incele…** gerçek nesne adları ve seçeneklerden üretilen salt okunur SQL'i açar. **Uygula** sonrasında native bakım sorgusu ve sonuç tablosu çalışır. OPTIMIZE'ın değişken ayarları da önizlemede bulunur; iptal sırasında değişken yazılmaz. Önceki fulltext kipi hata halinde de geri yüklenir. Nesne adlarındaki backtick doğru kaçırılır. |
| 39 - Mekânsal veri | `sqlide_import_spatial.py`. SHP, DBF, PRJ ve ogrinfo/ogr2ogr denetimleri, hedef katman/tablo/EPSG ve mevcut seçenekler korunur. Yeni **GDAL araç dizini** alanı gerçek klasör seçicisiyle çalışır. Seçim yerel uygulama tercihinde tutulur; araç yolu çözümlemesi önce bu dizine, sonra mevcut kurulum/PATH çözümlemesine bakar. Eksik araç göstergeleri dizin değişince yeniden hesaplanır. |
| 40 - Hücre/BLOB ayrıntısı | `grtui/binary_data_editor.cpp`. Hex/metin/görsel, dosyaya kaydet/yükle, salt okunur durum ve taslağa aktar eylemleri mevcut veri tamponunu kullanır. Uzunluk 64 bit bayt sayısıyla, hex sayfası gerçek aralıkla gösterilir. Adres sütunu düzenlenemez. Boş değer ve tam 8192 bayt katlarında son sayfa hesapları düzeltildi. Ham değer/kodlama değiştirilmedi. |
| 41 - CSV/JSON dışa aktarma | `sqlide_power_export_wizard.py`. Sütun seçimi, adet/offset, SQL kaynağı, dosya/format/kodlama seçenekleri ve sonuç aynı native motoru kullanır. Sütun adı genişliği form genişliğinin tamamını tüketmez. Boşluklar ve düğme etiketleri kompaktlaştırıldı. |

CSV/JSON ve SQL dökümü ayrı araçlardır. Bakım önizlemesi de nesne DDL ve sonuç hücresi uygulama işleyicilerinden ayrıdır. Var olan satır düzenleme tercihleri, bağlantı işlemleri ve native hata geri bildirimleri korunur.

## Tekrarlanabilir çalışma alanı derlemesi

Bu ilk çalışma alanı turunun varsayılan çıktısı `out/classic-workspace-ui` olur. Güncel görsel uyum çıktısı için aşağıdaki son etapta gösterilen `-Output out/classic-workspace-fidelity-ui` komutu kullanılır.

[Build-Workspace.ps1](../../Build-Workspace.ps1) PowerShell 7 ile hazırlanmış 8.0.47 native geliştirme ağacını ve önceden tamamlanmış çalışma zamanını kullanır. Visual Studio v143/C++/CLI, .NET başvuru derlemeleri, SWIG/JDK ve `out/workbench8-deps` önkoşulları [paket kılavuzundadır](../../README.md).

```powershell
pwsh -NoProfile -File packaging/classic-workbench/Build-Workspace.ps1
```

Kanonik kaynak `classic-workbench/` dizinidir. Betik yalnız Git'in bu ağaçta listelediği kaynakları native geliştirme ağacına eşler. Farklı yerel native değişiklikleri ilk kopyadan önce tespit eder ve çatışma listesini `out/classic-workspace-audit/workspace-source-conflicts.json` içine yazar. Önceki eşleme hash'leriyle güvenli tekrar çalıştırma desteklenir. Kaynak veya çıktı ağacında silme/mirror işlemi yapılmaz.

Çıktı `out/classic-workspace-ui/StarDataParadise.exe` olur. Var olan native `bin/x64/Release_OSS` çalışma zamanı eksik dosyaları sağlamak için kullanılır; `.git`, `.vs` ve Python bytecode kopyalanmaz. Temel kitaplıklar, Forms/C++CLI, SQL backend, sarmalayıcılar, nesne editörleri, db.search ve WinForms ana uygulaması sırasıyla ayrı çıktı dizinine derlenir. Post-build olayları kapalıdır. Python modülleri PrepareOutputDir düzenine göre, güncel XML ve görseller kaynak ağacından sahnelenir. Mevcut masaüstü uygulaması kapatılmaz.

`-Projects` ile hedefli yeniden derleme seçilebilir. Yeni çalışma alanı uygulaması çıktı DLL'lerini kullanırken yeniden derleme denenirse dosya kilidi hatası beklenir; betik süreç sonlandırmaz. Bu betik temiz bilgisayara tüm native bağımlılıkları kuran bir yükleyici değildir.

## Önceki uygulama turunun doğrulaması

| Durum | Kontrol |
| --- | --- |
| PASS | Yönetim Python dosyalarının ve SQLide Python dosyalarının AST denetimi, menü/araç çubuğu XML ayrıştırması. |
| PASS | Son paket denetiminde 23 Python AST, üç XML ve PowerShell ayrıştırması; 71 kanonik/native/eşleme manifesti hash'i; 21 sahnelenmiş Python/XML hash'i; beş Server sağlayıcı hash'i ve beş native JPEG açma kontrolü geçti. Kaynak boşluk denetimi ve yeni UI metinlerinde orta nokta araması temiz. |
| PASS | `workspace_actions_acceptance.py` beş gerçek-handler sınamasını geçti. Bakım iptalinde SQL yazılmaz; OPTIMIZE iptalinde yalnız mevcut değer okunur; onayda doğru sorgu çağrılır; hata halinde kip geri yüklenir; nesne adları kaçırılır. Testler kayıt tutan bir backend kullanır ve sunucuya bağlanmaz. |
| PASS | `Build-Workspace.ps1` PowerShell sözdizimi ayrıştırmasını geçti. Hazırlanmış native ağaçtan 17 hedef derlendi. Son değişikliklerden sonra ilgili backend, db.search ve WinForms hedefleri tekrar derlendi. Çıktı `out/classic-workspace-ui/StarDataParadise.exe`. |
| PASS | `test_sdp_tools.py` 12 sağlayıcı/adaptör sözleşme sınamasını geçti. Kapsam [araçlar kaydında](ARACLAR.md) açıklanır. |
| PASS | Gerçek native GRT/mforms çalışma zamanında beş ürün aracı, History SQL/sonuç tablosu, Decision üretimi ve model sağlayıcısı geçişi açıldı. `tools-native-result.json` sonucu başarılı. |
| PASS | Bağlı yeni GUI'de TLS, native NULL/boş metin ayrımı, unsigned BIGINT ve DECIMAL hassasiyeti, beş araç sayfasının dock edilmesi SELECT/SHOW ile doğrulandı. `tools-readonly-result.json` içinde `ok: true`, `server_writes: false`. |
| PASS | Gerçek derlenmiş hata bileşeninde ayrıntı açma, panoya kopyalama ve Esc ile normal kapanış geçti. Kanıt `out/classic-workspace-audit/error-dialog-probe-result.txt` ve yukarıdaki native görüntülerdir. |
| PASS | Yeni sonuç sekmeleri native çalışma zamanında çizildi; `SELECT 1 AS ui_kabul, NULL AS bos_deger` bir satırla Tablo görünümünde gösterildi. |
| Sonraki turda tamamlandı | Bu ilk turda pencere otomasyonu nedeniyle açık kalan Form, Sütun türleri, Sorgu ölçümleri ve Sorgu planı geçişleri, aşağıdaki görsel uyum etabında yeni derlemeyle canlı doğrulandı. |

Ham son kontroller `out/classic-workspace-audit/final-source-validation.json`, `final-powershell-validation.json`, `final-jpeg-validation.json` ve `final-text-separators.json` dosyalarındadır. `out/` ignore edilen yerel kabul çıktısıdır; kalıcı native görüntüler bu belgenin `uygulama/` alt klasöründedir.

```powershell
python packaging/classic-workbench/tests/workspace_actions_acceptance.py
python packaging/classic-workbench/tests/test_sdp_tools.py
```

## Kapsam sınırları

Bu kayıt kodda uygulanan yüzeyleri açıklar; görsellerin her durumunun canlı kabulden geçtiği anlamına gelmez. Yeni canlı kabul salt okunur sorgularla yapıldı; hesap/yetki, servis, DDL, veri aktarımı ve yedek geri yükleme yazmaları çalıştırılmadı. Bağlı test sunucusunda History günlüğü bulunmadı ve Compute kuyruğu kurulu değildi; bu durumlar hazır örnek veriyle doldurulmadı. Model kayıt metadata'sı erişilebilirdi. Mantıksal yedek create/restore için gerekli MySQL Shell çalışma zamanı Workbench içinde yoktur; bu iki eylem sunulmaz. Sağlayıcı kapsamı [araçlar kaydındadır](ARACLAR.md).

Arama önizlemesi bir örnek tablonun gerçek sorgusudur. Sonraki tabloların sorguları ve kalan toplam sınır gerçek arama sırasında çözülür. İleri native açıklamaların bir bölümü İngilizce kalır. Yeni bekleme görünümünün canlı SSH/iptal yarış matrisi, CSV/JSON için tüm veri türleri ve büyük dosyalar, canlı GDAL aktarımı, bütün motorlarda bakım ve tam DPI/erişilebilirlik kabulü ayrıca çalıştırılmalıdır. Önceki migration/TLS kabulü bu yeni kontrollerin yerine geçmez.

Help menüsünün MySQL dış sayfa bağlantıları kullanıcının isteğiyle sonraki aşamaya bırakılmıştır.

<a id="gorsel-uyum"></a>
## Görsele yakınlık ve sonuç alt sekmeleri - 19 Kasım 2026

Bu etap, kullanıcının SQL çalışma alanı görseline daha yakın yerleşim isteğini ve önceki turda açık kalan sonuç alt sekmesi kabulünü kapsar. Referanslar galerideki 01 SQL editörü, 02 sonuç alanı, 03 sorgu planı ve 07 şema inceleyicisidir. Başlık, ana sekmeler, menü ve alanlar sırası; kompakt boşluklar, düz ince kenarlar ve ` - ` metin ayracı sürer.

### SQL çalışma alanı ve şema gezgini

Çift Navigator/Şemalar başlığı yerine tek **Şema Gezgini** başlığı bulunur. Başlıktaki yenileme mevcut native komutu çalıştırır. **Nesne ara…** gerçek şema/nesne filtresidir; `*`, `?` ve şema/nesne kapsamını ayıran nokta davranışları korunur. Ağaç satırları 26 px, seçili satır yüzeyi düz nanedir. Yüksek kontrastta sistem seçimi korunur. Veritabanı, klasör, tablo, görünüm, yordam, fonksiyon, sütun, anahtar, ilişki ve tetikleyici ikonları aynı koyu çizgi ailesini kullanır. Yüklenme, bozuk görünüm ve anahtar durumları ayrı ikon/renk bilgisi taşır. Grup ve nesne bilgisi etiketleri Türkçedir.

SQL belge sekmeleri dosya ikonu, dosya adı, etkin yüzey, değişiklik işareti ve belirgin kapatma alanı olan düz **QueryDocument** stiliyle çizilir. Yeni boş tampon, kayıtlı dosyanın gerçek adı ve kapanış mevcut belge yaşam döngüsünü kullanır. Alan gezinmesi SQL/Veri/Şema/Performans/Yönetim/Araçlar ikonlarını paylaşır. **Çalıştır** koyu birincil düğmedir; yanındaki açılır menü tüm betik/seçim ve imleçteki ifade eylemlerini mevcut yürütme işleyicilerine bağlar. Aç, Kaydet, Durdur, Plan, otomatik commit, Commit ve Rollback aynı native oturumu kullanır.

Sonuç alanında **Tablo / Form / Sütun türleri / Sorgu ölçümleri / Sorgu planı** yatay sekmeleri bulunur. Geometri sütunu olan sonuçta **Mekânsal** girişi eklenir. Tablo ve Form komutlarında modern ortak ikonlar ve metinler kullanılır. Formda ilk/önceki/sonraki/son kayıt gezinmesi, kayıt sayacı, salt okunur durum, NULL ve boş metin ayrımı gerçek sonuç nesnesinden alınır. Sıfır satırda alanlar boş durumu gösterir ve gezinme komutları devre dışıdır.

### Plan ve ölçüm düzeltmeleri

Plan, etkin SQL tamponu sonradan değiştirilse de ilgili sonucu üreten gerçek SQL ifadesinden alınır. Eski MySQL sürüm izin listesine dayanmak yerine sunucunun gerçek EXPLAIN yanıtı kullanılır. Geleneksel biçim desteklenmezse düz EXPLAIN denenir; JSON ayrıca okunur. Native **Ağaç / Tablo / JSON** sayfaları aynı yanıtı sunar. 26.x sunucunun JSON planı, eski `query_block` şekline dönüştürülmeden veya örnek planla değiştirilmeden gösterilir. JSON görünümü salt okunurdur.

Plan alınamadığında açıklama ve yeniden deneme yüzeyi korunur; başarılı plan oluşmadan yer tutucu kaldırılmaz. Sorgu ölçümleri gerçek native ölçüm nesnesini kullanır. Sunucunun toplamamış olduğu ayrıntılar için açıklama gösterilir. Sütun türleri gerçek sonuç metadata'sından, sorgu geçmişi ise seçilen tarihteki gerçek yerel kayıtlardan gelir.

### Son derleme ve kabul

Güncel çıktı ayrı `out/classic-workspace-fidelity-ui` dizinindedir. Önceki çalışma zamanı seed olarak kullanılır; kullanıcı ayarları ve açık eski uygulama korunur. Aynı çıktıyı üretmek için depo kökünde:

```powershell
pwsh -NoProfile -File packaging/classic-workbench/Build-Workspace.ps1 -Output out/classic-workspace-fidelity-ui -RuntimeSeed out/classic-workspace-ui
```

| Durum | Kontrol |
| --- | --- |
| PASS | 18 native hedefin tam derlemesi, `wbprivate`/`sqlide.fe` hedefli yeniden derlemesi ve son hata/yeniden deneme düzeltmesinden sonra `wbprivate` derlemesi tamamlandı. |
| PASS | 24 davranış sınaması: 12 ürün aracı, beş çalışma alanı işlemi ve yedi sorgu planı kabulü. Son grup gerçek sonuç SQL'inin seçilmesini, EXPLAIN biçimi geri dönüşünü, hata raporunu ve native sonuç referansının serbest bırakılmasını kapsar. |
| PASS | Yeni native uygulamada Tablo, Form, Sütun türleri, Sorgu ölçümleri ve Sorgu planı tek tek açıldı. Önceki turun açık canlı kontrolü tamamlandı. |
| PASS | Planın Ağaç/Tablo/JSON sayfaları gerçek 26.x sunucu yanıtıyla açıldı. JSON planının yeni biçimi gösterildi. |
| PASS | Geçersiz SQL için plan isteği aynı Plan sekmesinde satır içi hata gösterdi ve sekme korunarak yeniden deneme eylemi erişilebilir kaldı. Aynı sorgu tamponunda geçerli sorgu dosyası açıldıktan sonra **Planı yeniden al**, gerçek JSON v2 plan ağacını gösterdi. |
| PASS | İki satırlı Form sonucunda 1/2 → 2/2 gezinmesi çalıştı; NULL ile boş metin ayrı gösterildi. Salt okunur sıfır satır sonucunda boş durum ve devre dışı gezinme doğrulandı. POINT/NULL içeren sonuç Formda açıldı. |
| PASS | Geometri sütununda Mekânsal sekmesi koşullu olarak açıldı ve katman yüklendi. Haritadaki geometrinin kesin konum/izdüşüm doğruluğu bu kabulün iddiası değildir. |
| PASS | Birincil Çalıştır düğmesi iki SELECT'i iki sonuca yürüttü; açılır menüdeki imleçteki ifade eylemi yalnız seçilen ifadeyi yürüttü. Boş sorgu sekmesinin kapanışı ve kayıtlı SQL dosyasının gerçek adının sekmede görünmesi doğrulandı. |
| PASS | Sorgu geçmişinde tarih seçimi iki gerçek SQL kaydını gösterdi. |
| PASS | Satır sınırı 10 seçildi ve iki satırlık sabit SELECT sonucu alındı; kontrol 1000'e geri getirildi. Bu kontrol, on satırdan büyük veri kümelerinde sınır uygulama matrisi değildir. |
| PASS | Şema gezgini genişletme ve nane seçim yüzeyi gözlendi. `new_db` filtresi gerçek ağaçta yalnız eşleşen bir şemayı göstererek diğer iki şemayı gizledi. Filtre temizlendiğinde `new_db_for_ai`, `sdp_models` ve `sys` şemalarının üçü de geri geldi. |
| PASS | Son çalışan uygulamada gerçek `ai_exam_demo` tablosu seçilip salt okunur SELECT ile sekiz satır gösterildi. En son gerçek SELECT sorgu geçmişinde yer aldı. Yeni `classic-workspace-fidelity-ui` uygulaması bu sonuçla masaüstünde açık bırakıldı. |
| PASS | Son native pakette salt okunur kabul yeniden çalıştı: TLS, NULL/boş metin ayrımı, unsigned BIGINT ve DECIMAL tamlığı doğrulandı. `out/classic-workspace-audit/fidelity-native-readonly.json` içinde `ok: true`, `server_writes: false`; işlemler SELECT/SHOW ile sınırlı. |
| PASS | 305 kanonik/native/eşleme manifesti kaynak hash'i, 22 çalışma zamanı Python/XML dosyası, 219 ikon/SVG/manifest/lisans kaynağı, 13 zorunlu çalışma zamanı dosyası ve beş x64 PE dosyası denetlendi. `fidelity-runtime-validation.json`. |
| PASS | 26 Python AST, üç XML, PowerShell ayrıştırması, yeni UI metin ayraçları ve kaynak boşluk denetimi geçti. Tam 160 PNG'nin CMake/MSI kurulum listelerinde bulunduğu doğrulandı. `fidelity-source-validation.json`, `fidelity-powershell-validation.json`, `fidelity-icon-install-validation.json`. |

Ham raporlar Workbench `out/classic-workspace-audit/` altındadır. Canlı kabul yalnız okuma sorguları ve yerel UI işlemleriyle yürütüldü; hesap/yetki, veri yazma, servis veya yedek geri yükleme işlemi başlatılmadı. İleri native yardım/metadata metinlerinin bir bölümü İngilizce kalır. Installer üretimi/kur-kaldır ve tam DPI/yüksek kontrast/ekran okuyucu matrisi bu etabın kabulü değildir.

İlk CLI kabul çağrısında dosya adının `--run-python` ifadesi olarak verilmesi test koşucusunda NameError üretti. Çağrı `--run-script` ile düzeltildi ve yukarıdaki native kabul geçti; bu hata ürün SQL veya bağlantı davranışından kaynaklanmadı.

### Son native görüntüler

| Görüntü | Gösterilen durum |
| --- | --- |
| [Son SQL çalışma alanı](uygulama/fidelity/sql-final.jpg) | Genişletilmiş şema gezgini, seçili gerçek tablo, sekiz satırlık sonuç ve en son sorgu geçmişi |
| [Tablo](uygulama/fidelity/tablo.jpg) | Yeni SQL belge sekmesi, araç çubuğu, şema gezgini ve gerçek sonuç |
| [Çalıştır menüsü](uygulama/fidelity/calistir-menu.jpg) | Gerçek yürütme seçenekleri |
| [Tüm betik](uygulama/fidelity/calistir-tum-betik.jpg) | İki SELECT için iki sonuç |
| [İmleçteki ifade](uygulama/fidelity/calistir-imlec.jpg) | Yalnız ilgili ifade sonucu |
| [Form - NULL](uygulama/fidelity/form-null.jpg) | İlk kayıt ve NULL |
| [Form - boş metin](uygulama/fidelity/form-bos-metin.jpg) | İkinci kayıt ve boş metin |
| [Form - sıfır satır](uygulama/fidelity/form-sifir-satir.jpg) | Boş durum ve devre dışı gezinme |
| [Form - geometri](uygulama/fidelity/form-null-geometri.jpg) | POINT ve NULL alanları |
| [Sütun türleri](uygulama/fidelity/sutun-turleri.jpg) | Native sonuç metadata'sı |
| [Sorgu ölçümleri](uygulama/fidelity/sorgu-olcumleri.jpg) | Gerçek ölçüm durumu |
| [Plan - Ağaç](uygulama/fidelity/plan-agac.jpg) | Gerçek JSON yanıtının hiyerarşisi |
| [Plan - Tablo](uygulama/fidelity/plan-tablo.jpg) | Geleneksel EXPLAIN sonucu |
| [Plan - JSON](uygulama/fidelity/plan-json.jpg) | Salt okunur sunucu yanıtı |
| [Plan - hata](uygulama/fidelity/plan-hata.jpg) | Aynı sekmede açıklama ve yeniden deneme eylemi |
| [Plan - yeniden deneme](uygulama/fidelity/plan-yeniden-deneme.jpg) | Düzeltilmiş sorguyla gerçek JSON v2 ağacı |
| [Satır sınırı](uygulama/fidelity/satir-siniri.jpg) | Sınır seçimiyle sabit iki satırlık sonuç |
| [Şema araması](uygulama/fidelity/sema-arama.jpg) | Gerçek şema filtresiyle daralan ağaç |
| [Mekânsal](uygulama/fidelity/mekansal.jpg) | Geometri sütununa bağlı katman |
| [Sorgu geçmişi](uygulama/fidelity/sorgu-gecmisi.jpg) | Tarihe göre gerçek yerel sorgu kayıtları |

### İkon kökeni ve paket kayıtları

Ortak ikonlar [Tabler Icons](https://github.com/tabler/tabler-icons) kaynağının sabit `85e870dbe8680c2d6deefd062122d30ef1544a41` commit'inden alınır. 57 özgün SVG ve [MIT lisansı](https://github.com/tabler/tabler-icons/blob/85e870dbe8680c2d6deefd062122d30ef1544a41/LICENSE) `classic-workbench/images/ui/tabler-source/` altında korunur. 160 adet PNG16/32, kaynak ve çıktı SHA-256 kayıtlarıyla `images/ui/sdp-tabler-icons.json` manifestinde bulunur. Şema ağacı mevcut `images/grt/structs/*.side.*.png` yollarını korur; diğer kontroller `images/ui/sdp_<ad>.png` ve `@2x` sürümlerini paylaşır.

`Generate-WorkspaceIcons.py` çevrimdışı rasterleştirmeyi ve CMake/WiX dosya listesi eşlemesini tekrarlar; `--install-lists-only` ağ veya rasterleştirme gerektirmez. CMake ve MSI kayıtlarında UI için 121, şema ikonları için 22 eksik dosya kaydı eklendi; mevcut File ID'leri ve bileşen GUID'leri korundu. MSI lisansı `images/ui/sdp-tabler-license.txt` adıyla kurar. Eski `PrepareOutputDir.cmd` yolu da manifesti ve özgün lisansı sahneler. Liste ve XML denetimi installer kurulum testi yerine geçmez.

Bu etapta dokunulan ana kaynak aileleri:

| Yüzey | Kaynak |
| --- | --- |
| Şema gezgini | `wb_live_schema_tree.cpp`, `wb_sql_editor_tree_controller.cpp`, `advanced_sidebar.cpp`, `wf_treeview.cpp` |
| Belge sekmesi ve komutlar | `FlatTabControl.cs`, `SqlIdeForm.cs`, `wf_toolbar.cpp`, `wb_sql_editor_panel.cpp`, `dbquery_toolbar.xml` |
| Sonuçlar, Form ve geçmiş | `wb_sql_editor_result_panel.cpp`, `recordset_be.cpp`, `result_form_view.cpp`, `RecordsetView.cs`, `db_sql_editor_history_be.cpp` |
| Gerçek sorgu planı | `plugins/wb.query.analysis/wb_query_analysis_grt.py`, `tests/query_plan_acceptance.py` |
| İkon üretimi ve kurulum listeleri | `Generate-WorkspaceIcons.py`, `images/ui/CMakeLists.txt`, `images/grt/CMakeLists.txt`, `mysql_workbench_fragment.xml`, `PrepareOutputDir.cmd` |
