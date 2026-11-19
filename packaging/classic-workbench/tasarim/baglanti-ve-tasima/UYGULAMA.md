# Classic bağlantı ve veri taşıma ekranları · Uygulama kaydı

19 Kasım 2026. Kaynak Workbench 8.0.47, `dc3590dbfbee29c87e201d4ef53607e5deb0e4f9`. Kullanıcının seçtiği [kompakt form ve diğer taslaklar](README.md), [ana sayfa kurallarıyla](../ANA_SAYFA.md) birlikte uygulandı. Kaynak değişiklikleri yeniden üretilebilir [Classic yamasındadır](../../workbench-8.0.47.patch); `out/` altındaki kaynak ağacı ve ikililer build çıktısıdır.

## Uygulanan ekranlar

- **Yeni bağlantı:** 740 × 365 DIP başlangıç ölçüsü; profil adı ve yöntem; tek sırada Genel / TLS / SSL / Gelişmiş sekmeleri. Sunucu ve 70 DIP port aynı satırda. TLS ve uzun gelişmiş alanlarda yükseklik seçili sayfanın gerçek tercih edilen ölçüsüne göre büyür; Genel'e dönünce küçülür. Sağdaki eski yardım sütunu kaldırıldı; yardım alanların ipuçlarında bulunur. Parolanın kasada bulunması gerçek vault sorgusundan gelir. Sunucu yönetimi, sınama, vazgeçme ve kaydetme mevcut native işlevleri kullanır. Ad ve yinelenen profil denetimleri korunur.
- **TLS:** beş native kipin sayısal değerleri korunarak Türkçe adlar gösterilir. CA, istemci sertifikası, özel anahtar ve şifre takımları kendi satırlarındadır; dosya seçiciler gerçek Windows iletişim kutularını açar. Sertifika üretme ve klasör araçları Gelişmiş sekmesine taşındı.
- **Profilleri yönet:** 980 × 570 DIP başlangıç ölçüsü; 200 DIP profil listesi, ana sayfadaki kalıcı piksel avatarları, 32 DIP satırlar, nane seçim ve turkuaz işaret. Genel / TLS / SSL / Gelişmiş / Uzaktan yönetim / Sistem profili tek sekme sırasıdır. Oluşturma, silme, çoğaltma ve sıralama native kayıtları düzenler. Sınama sonucu seçili profilin yanında gösterilir.
- **SSH:** Kapalı / Windows / SSH seçimi; sunucu, 70 DIP port, kullanıcı, kasa eylemleri, anahtar kullanımı ve dosya seçimi gerçek `ServerInstance` alanlarına bağlıdır. Yerel TCP profillerinde gereksiz uzak yönetim, mevcut native kurala göre kapalıdır. Bağlantı yöntemindeki SSH tüneli ve uzaktan sunucu yönetimi ayrı native yetenekler olarak korunur.
- **Veri Taşıma:** açılışta kaynak ve hedef yan yanadır. Kayıtlı kaynaklar, hedefler, bağlantı ayarları ve ODBC yöneticisi gerçek işlevlere bağlıdır. **Şemaları oku** keşfe gider. Solda dört ana aşama vardır; mevcut motorun 15 alt sayfası ve geri gezinme geçmişi korunur.
- **SQL önizlemesi:** hedef nesnenin gerçekten üretilmiş SQL'i salt okunur editörde gösterilir. Nesne seçimi, tablo sayısı, kaynak/hedef özeti ve **Betiği kaydet…** işlevseldir. **Aktarım seçenekleri** mevcut uygulama seçeneklerine geçer. Önizleme hedefe yazmaz; önceden uygulanmış bir hedefe geri dönülürse durum metni bunu açıklar.
- **Gezinme:** Veri Taşıma içindeki Bağlantılar / Modeller düğmeleri ve menüler gerçek ana sayfaya döner. Windows AppView seçimi, TabPage etiketine güvenmek yerine gerçek belge nesnesini kullanacak şekilde düzeltildi.

Yüzeyler `#EDF2F4`, komut şeritleri `#E7EEEF`, ana yazı `#102629`, seçim `#DDEBE4`, vurgu `#098874`, ana eylem `#043133` kullanır. Form metinleri Segoe UI 9,75 pt, teknik değerler Consolas 9,75 pt; ana düğmeler 26 DIP, dış boşluk 12 DIP'tir. Classic sekme stili ayrı eklenerek eski enum değerleri korundu. Sekme genişlikleri seçili yazının gerçek GDI ölçüsüyle hesaplanır ve font değişiminde yenilenir.

## Kabul ve kanıt

Yollar Workbench depo köküne göredir. Otomatik aktarımı tekrar çalıştırma adımları [test kılavuzundadır](../../tests/README.md).

| Sonuç | Kontrol | Kanıt |
| --- | --- | --- |
| PASS | Tam `Release_OSS / x64` çözüm derlemesi | `out/mysql-migration/dialog-ui-build-3.log` |
| PASS | Native Forms, C++/CLI, wbpublic, wbprivate ve C# hedefleri; seçili sekme metni ve içeriğe göre form boyutu düzeltmeleri | `dialog-ui-build-5.log`, `dialog-ui-build-6.log`, `dialog-ui-build-7.log`, `dialog-ui-build-8.log`, `dialog-ui-build-9.log`, `dialog-ui-build-10.log` aynı günlük dizininde |
| PASS | Son paketleme; bağımlılıklar ve yerel yardımcı araçların açılışı | `out/mysql-migration/dialog-ui-package-final.log` |
| PASS | Yan yana başlangıç ve 15 gerçek wizard sayfası; ayrı salt okunur SQL önizlemesi | `out/mysql-migration/dialog-ui-migration-built-final/result.json` |
| PASS | Önizlemede 16 DDL/DML sayacı değişmedi, hedef şema oluşmadı, kaynak veriler değişmedi | Aynı JSON: `preview_zero_writes: true` |
| PASS | TLS gerektiren geçici sunucuda gerçek `wbcopytables`; üç satırın BIGINT, DECIMAL, UTF-8 VARCHAR/TEXT, NULL, boş binary ve tüm 256 BLOB baytı bağımsız CLI ile eşleşti | Aynı JSON: `independent_cli_verified: true`; `out/mysql-migration/dialog-ui-migration-built-final.log` |
| PASS | Canlı **Betiği kaydet…** Windows dosya seçicisiyle SQL kaydı; üretilmiş SQL ile birebir UTF-8 karşılaştırması | `out/classic-dialog-acceptance/preview-saved.sql` ve `out/mysql-migration/dialog-ui-migration-acceptance-3/result.json` |
| PASS | İzole native profil yöneticisinde SSH portu 22 → 2222; hem `ssh.port` hem `ssh.tunnelPort` kaydedildi; sunucu, anahtar yolu ve TLS kipi dosyadan geri okundu | `out/classic-dialog-acceptance/profile-ui-result.json`, `profile-ui-final-test/connections.xml`, `profile-ui-final-test/server_instances.xml` |
| PASS | Son pakette normal kullanıcı profilinin **Bağlantıyı sına** düğmesi gerçek `127.0.0.1:3197` sunucusunu doğruladı; **Bağlantı sınaması başarılı** metni gösterildi | `out/classic-dialog-acceptance/profiles-general-ui.jpg` |
| PASS | Kompakt Genel → TLS → Gelişmiş → Genel geçişi; TLS'nin beş satırı ve gelişmiş metin alanı kesilmeden görünüyor | `out/classic-dialog-acceptance/new-connection-ui.jpg`, `new-connection-tls-ui.jpg`, `new-connection-advanced-ui.jpg` |
| PASS | Son pakette Veri Taşıma başlangıcı açıldı; üstteki Modeller düğmesi gerçek son kullanılan model listesini açtı, Bağlantılar sekmesine dönüldü | `out/classic-dialog-acceptance/migration-start-ui.jpg`, `migration-models-navigation-ui.jpg` |
| PASS | 18 migration Python modülünün sözdizimi | Python `ast.parse` |
| PASS | 40 kaynak dosyasının güncel bölümleri patch'e aktarıldı; tam patch temiz kaynak HEAD'ine geçici index ile uygulandı; kapsam dışı bölümler korundu | `out/mysql-migration/dialog-ui-patch-check.log` |

Canlı ekran kanıtları `out/classic-dialog-acceptance/` altındadır: `profiles-general-ui.jpg`, `profiles-tls-ui.jpg`, `profiles-ssh-ui.jpg`, `new-connection-ui.jpg`, `new-connection-tls-ui.jpg`, `migration-start-ui.jpg`, `sql-preview-ui.jpg`. SQL görüntüsünde yalnız kabul sunucusunun ürettiği örnek SQL kullanılır; mevcut kullanıcı veritabanına yazılmaz.

Son patch SHA-256: `845cb26ccc8e11f3206f13ab0d7b77d5338c29c76e22ec7f958f51b8b5d6c799`.

## Giderilen hatalar ve kapsam

İlk derlemelerde avatar yolu için yanlış parametre türü, TextBox'a uygulanmış TextEntry özellikleri ve değiştirilmiş `std::bind` imzası derlemeyi durdurdu; uyumlu API'lerle giderildi. İlk migration başlatmalarında bulunmayan `newMenuBar` çağrısı ve boş kısayolu olan ayırıcı öğe hata verdi; `MenuBar` ve yalnız dolu kısayolları kopyalama kullanıldı. Sonraki kabul geçti.

Profil yöneticisinin ilk canlı açılışında avatar yolunda değişken parametreli `joinPath` çağrısının eksik sonlandırıcısı erişim ihlaline yol açtı. İki parametreli `makePath` ile değiştirildi; son paketle yönetici tekrar tekrar açılıp kapatıldı. %125 ölçekte seçili TLS başlığının kesilmesi GDI metin ölçüsü, tek satır çizimi ve font/layout önbelleği yenilemesiyle giderildi. İzole profil kabul betiğinin ilk XML yardımcı çağrısındaki argüman sırası hatası da test betiğinde düzeltildi; native kaydın doğru yazıldığı ve son kabul sonucu ayrıca doğrulandı.

Kompakt pencerenin ilk TLS kontrolünde beşinci satır kesildi. Sabit pencere yüksekliği yerine seçili sayfanın tercih edilen yüksekliği hesaplandı; son native build ve Genel/TLS/Gelişmiş geçişi canlı doğrulandı. Normal profili sandbox içinden açan bir son kontrol AppData XML kaydına izin verilmediği için açılışta kayıt hatası, kapanışta native istisna verdi (`dialog-ui-final-desktop.log`). Normal masaüstü izinleriyle açılış ve temiz kapanış geçti; son uygulama `dialog-ui-final-normal-desktop-10.log` oturumunda açık bırakıldı. Kullanıcı dosyalarının izinleri değiştirilmedi.

Kullanıcının mevcut kaynak, ayar ve profilleri korunur; iki sentetik SSH profili yalnız `out/classic-dialog-acceptance/profile-ui-final-test/` içindedir. SSH alanlarının düzenleme ve saklama kabulü geçti; erişilebilir bir SSH sunucusu sağlanmadığı için canlı SSH oturumu/tüneli bu etapta sınanmadı. MySQL dışındaki kaynaklar için uygun x64 ODBC/DBAPI sürücüsü gerekir; bu sürücüler için aktarım kabulü yapılmadı. Windows kontrolü %125 ölçekli bu masaüstünde yapıldı; tüm DPI ve yüksek kontrast matrisi çalıştırılmadı. Eski gelişmiş sistem araçlarının bazı yardım/hata metinleri İngilizce kalır.

Kanıt dosyalarının uzantı kontrolü, masaüstü yakalamalarının JPEG olduğunu belirledi. İlk `.png` adları görüntü verisine dokunmadan `.jpg` olarak düzeltildi. Ek decoder denemesi yerel Python'da PIL bulunmadığı için çalışmadı; Windows `System.Drawing.Image` dokuz dosyanın tamamını JPEG olarak açıp boyutlarını doğruladı. Son patch özeti, belge bağlantıları, görsel dosyaları, kaydedilmiş SQL'in birebir eşleşmesi ve sonuç JSON'larının ortak denetimi geçti.
