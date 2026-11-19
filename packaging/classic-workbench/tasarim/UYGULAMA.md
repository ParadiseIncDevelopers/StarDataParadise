# Classic ana sayfa · Uygulama ve kabul kaydı

18 Kasım 2026. Kaynak tabanı Workbench 8.0.47, `dc3590dbfbee29c87e201d4ef53607e5deb0e4f9`. Tasarım referansı [ANA_SAYFA.md](ANA_SAYFA.md); yeniden üretilebilir değişiklikler [Classic yamasında](../workbench-8.0.47.patch).

## Uygulanan davranış

- **Bağlantılar:** kompakt sütunlu liste, kalıcı nane/mor piksel avatarları, profil/kullanıcı/sunucu araması, görünür satır seçimi, ok tuşları ve Enter ile bağlantı açma. Bağlan, profili düzenle, bağlantıyı sına ve yeni bağlantı mevcut native işlevlere bağlıdır. Ortam atanmamışsa çizgi gösterilir. Karşılama tercihi ve ana sayfanın açık/koyu teması saklanır.
- **Modeller:** gerçek son kullanılan `.mwb` listesi, yeni model, model açma ve sunucudan model çıkarma girişleri. EER araç çubuğu yataydır; Tablo ve İlişki ekle etiketleri native araçlara bağlıdır. Yeni model varsayılanları yeşil başlık ve Segoe UI/Consolas kullanır. Kullanıcının daha önce kaydettiği renk/font tercihleri korunur.
- **Veri Taşıma:** kaynak/hedef, tablo seçimi, tür ve ad eşleştirme, SQL önizleme, şema uygulama ve veri kopyalama gerçek migration motorunu kullanır. Mevcut alt adımlar Türkçe gruplarda korunur. Önizleme ile hedefe yazma ayrı adımlardır.
- **Menüler:** ana sayfada Dosya, Çalışma alanı, Görünüm ve Yardım; yerel klavye gezinmesi ve menü kısayolları korunur. Belge bağlamlarının SQL/model komutları kendi menülerinde kalır.

Canlı kabulde eski aktarım aracının BLOB `00FF` değerini `00` olarak taşıdığı bulundu. Binary hedef sütunları artık uzunluğu korunarak SQL hex literal ile yazılır; TEXT karakter dönüşümü devam eder. BLOB/VARBINARY düzeltmesi de aynı yamaya dahildir.

## Doğrulama

Günlük yolları Workbench depo köküne göredir. Tekrar çalıştırma ayrıntıları [test kılavuzunda](../tests/README.md).

| Sonuç | Kontrol | Kanıt |
| --- | --- | --- |
| PASS | Tam `Release_OSS / x64` çözüm derlemesi; son görsel düzeltmelerin Forms, C++/CLI ve C# hedeflerinde derlenmesi | `out/mysql-migration/home-ui-build-4.log`, `home-ui-build-5.log`, `home-ui-build-6.log`, `home-ui-build-7.log`, `home-ui-build-8.log` |
| PASS | Native Connector/C++ ile geçici StarDataParadise sunucusunda TLS kimlik doğrulaması, yanlış CA reddi, BIGINT/DECIMAL/BLOB, ANALYZE ve rollback | `out/mysql-migration/home-ui-native-acceptance.log` |
| PASS | 14 gerçek wizard sayfası; önizlemede 16 DDL/DML sayacı değişmedi; hedef şema önizlemede oluşmadı | `out/mysql-migration/home-ui-migration-acceptance/result.json` |
| PASS | Gerçek `wbcopytables` ile üç sentetik satır; tüm 256 bayt, NUL/quote/backslash/FF, boş BLOB/VARBINARY, UTF-8 VARCHAR/TEXT, BIGINT, DECIMAL ve NULL; bağımsız CLI karşılaştırması | Standart ve `utf8mb4` için `out/mysql-migration/home-ui-migration-acceptance/result.json`, `home-ui-migration-utf8-acceptance/result.json` |
| PASS | Model oluşturma, iki tablo/beş alan/iki PK/bir FK, iki diyagram figürü ve ilişki, gerçek `.mwb` kaydı | `out/classic-home-acceptance/model-fixture-result.json` |
| PASS | Kaydedilen modeli normal GUI'de Ctrl+O ile yeniden açma ve diyagramı gösterme; Tablo/İlişki ekle etiketleri kesilmeden görünüyor | `out/classic-home-acceptance/model-reopened-ui.png` |
| PASS | Normal profilde arama, görünür seçimin güncellenmesi, Enter ile `test` SQL oturumu, açık/koyu tema ve Veri Taşıma giriş sekmesi | Canlı Windows UI kontrolü; `out/classic-home-acceptance/migration-ui.png` |
| PASS | Son paketleme ve yeniden açılış; %125 ölçekte tam Bağlan etiketi, vektör Enter simgesi, temayla birlikte yenilenen menü | `out/mysql-migration/home-ui-package-final.log`, `out/classic-home-acceptance/home-final-ui.png` |
| PASS | Tam yamanın temiz kaynak HEAD'ine geçici index üzerinden uygulanabilirliği; mevcut çalışma index'i ve kapsam dışı patch bölümleri korundu | `out/mysql-migration/home-ui-patch-check.log` |

İlk derleme kısıtlı süreçte sürücü eşlemesine, ikinci deneme açık uygulamanın DLL kilidine takıldı. Üçüncü derleme, devam eden menü düzenlemesi tamamlanmadan C# hedefine ulaştı. Tamamlanmış kaynaklarla dördüncü derleme ve sonraki hedefli derlemeler geçti. İlk migration denemesi sandbox'ın yerel soket kısıtına takıldı. Test hazırlığında upstream ConnectionPanel setter'ının gereksiz istisnası ve gerçek BLOB aktarım kaybı ayrıca saptandı; sonuçlar ve çözüm test kılavuzunda açıklanır. İlk yama denetiminin CRLF satır sonlarını boşluk hatası sayması `cr-at-eol` denetim ayarıyla giderildi; gerçek boşluk denetimi korunur.

## Yerel durum ve kapsam sınırları

`localhost` kayıtlı profilinin varsayılan şeması `sdp_razor_demo`; canlı sunucu bu şema için `Unknown database` döndürdü. Kayıtlı profil değiştirilmedi ve şema oluşturulmadı. `test` profili aynı uç noktada gerçek SQL oturumunu açtı. Bağlantı altyapısının ayrıca izole sunucuda başarılı kabulü vardır.

Bu pilot ana sayfa ve bölüm girişlerini yeniler. EER tuvalinin native beyaz zemini ve 10 birim ızgarası korunur; mevcut belge modelleri yeniden biçimlendirilmez. Model/taşıma araçlarının bazı alt panel ve hata metinleri native İngilizce metinleri kullanır. Migration'ın güvenilir mevcut alt adımları, taslağın dört ana aşamasının altında ayrı sayfalar olarak kalır.

Canlı kontrol bu Windows oturumunda %125 ölçekle (`AppliedDPI=120`), 1482×955 ve 1920×1080 pencere görüntülerinde yapıldı. %100/%150/%200, 1366×768 ve Windows yüksek kontrast matrisinin tamamı çalıştırılmadı. Windows native DrawBox odak konturunun koyu temadaki görünümü ayrıca iyileştirilebilir; metin ve seçim için yüksek kontrast sistem rengi desteği bağlantı alanında uygulanmıştır.

Son görsel kontrolde ana sayfa menüsünün açık/koyu tema değişimine aynı anda uyduğu doğrulandı. %125 ölçekte kesilen Bağlan yazısı için düğme 96 DIP, eylem sütunu 104 DIP olarak ayarlandı; Enter işareti font glifi yerine vektör olarak çizilir. Son paket tekrar açılarak metnin kesilmediği doğrulandı ve uygulama ana sayfada açık bırakıldı.
