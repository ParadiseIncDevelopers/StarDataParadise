# StarDataParadise Classic · Ana sayfa tasarım kuralları

Windows x64 için Classic Workbench 8.0.47'nin açılış ekranı **Bağlantılar** sekmesidir. Kullanıcı aynı sunucudaki iki hesabı listede ayırt edebilmeli ve seçili satırdan SQL oturumunu açabilmelidir. **Modeller** ve **Veri Taşıma** aynı pencere kabuğunda kendi araçlarını gösterir. Bu belge, bu üç ekranın uygulama referansıdır.

| Görsel referans | Görseldeki durum |
| --- | --- |
| [Bağlantılar](baglantilar.png) | Karşılama açık, iki kayıtlı profil var, `localhost` seçili. |
| [Modeller](modeller.png) | Açık bir `.mwb` dosyasında iki tablo ve bir bire-çok ilişki var. |
| [Veri Taşıma](veri-tasima.png) | Kaynak ve hedef seçilmiş, kullanıcı iki tabloyu eşleştiriyor. |

## 1. Ölçüm ve uygulama birimleri

Üç PNG de **1774 × 887 fiziksel piksel**. Bağlantılar görselinde üst menü, karşılama ve sekme şeridinin alt çizgileri sırasıyla **y = 62, 203 ve 295 px** konumunda. Profil sütun başlığının alt çizgisi **y = 463 px**, ikinci kaydın alt kenarı yaklaşık **y = 639 px**. Bu noktalar görseli karşılaştırmak içindir. PNG'lerin pencere DPI değeri, yazı tipi ailesi ve özgün yerleşim kutuları görüntüden kesin olarak belirlenemez.

Aşağıdaki yerleşim ve tipografi değerleri, istenen kompakt görünüm için **uygulama hedefleridir**. Ölçü birimi DIP'tir (96 DPI'da 1 fiziksel piksel). Konumu, çizimi ve tıklama alanını aynı yerleşim dikdörtgeninden üret. DPI dönüşümünü bir kez uygula. Erişilebilirlik için metin büyütüldüğünde satır ve kontrol yüksekliği içeriğe göre artar.

## 2. Renkler

Referans PNG'lerde geniş, düz alanların içinden alınan RGB örnekleri aşağıdadır. Oluşturulan görsellerde komşu pikseller arasında küçük farklar vardır. **Uygulama** sütunundaki düz sRGB değerleri kullanılır. Koyu tema için değerler aynı rollerin tasarım karşılığıdır. Varsayılan tema açıktır.

| Rol | Açık referans örneği | Uygulama · açık | Uygulama · koyu |
| --- | --- | --- | --- |
| Genel yüzey | `#EDF2F4` | `#EDF2F4` | `#172326` |
| Karşılama ve araç yüzeyi | `#E7EEEF` | `#E7EEEF` | `#1D2C30` |
| Ana yazı ve çizgi ikon | `#001D1F` | `#102629` | `#ECF4F3` |
| İkincil yazı | `#4F5759` | `#4F5759` | `#B7C7C8` |
| İnce bölüm ayracı | `#A3B4BC` | `#A3B4BC` | `#405359` |
| Seçili kayıt zemini | `#DDEBE4` | `#DDEBE4` | `#234238` |
| Etkin sekmenin alt çizgisi | `#098874` | `#098874` | `#66CEB6` |
| **Bağlan** dolgusu ve yazısı | Dolgu `#043133` | `#043133` / `#FFFFFF` | `#9ADBCB` / `#102629` |
| **Önizlemeye geç** dolgusu ve yazısı | Dolgu yaklaşık `#096E56` | `#096E56` / `#FFFFFF` | `#76D3B0` / `#102629` |
| İkincil eylem ve klavye odağı | Hızlı başlangıç yazısında `#0B5F41` | `#0B5F41` | `#8FE4C5` |
| Model tuvali ve tablo başlığı | `#F3F6F9` / `#499085` | `#F3F6F9` / `#336F66` | `#19272A` / `#345F59` |

Model tablo başlığında beyaz küçük yazı için referans tonu koyulaştırılmıştır. Marka yıldızının ana mavi ve turkuazı mevcut 128 px ürün ikonunda yaklaşık `#002AD0` ve `#02F7E2` olarak örneklendi. Yıldızda ürün görselini, komutlarda tablodaki eylem renklerini kullan.

### Düğme ve seçim durumları

| Durum | Açık tema | Koyu tema |
| --- | --- | --- |
| Bağlan · üzerine gelme / basılı | `#104748` / `#022527` | `#ADE7D8` / `#78BEAE` |
| İleri adım · üzerine gelme / basılı | `#075D49` / `#064A3A` | `#92E0C1` / `#65BC9D` |
| Düz eylem veya satır · üzerine gelme | `#E1E9EB` | `#2A3A3E` |
| Devre dışı dolgu / yazı | `#DCE3E4` / `#53656A` | `#2A393D` / `#A7B4B6` |

- **Bağlan** ve ileri adım düğmesi 26 DIP yüksekliğinde, yatay iç boşluğu 10, ikon–metin aralığı 6 DIP'tir. Köşe yarıçapı 2 DIP'tir. **Bağlan** için sağ alt köşede 3 DIP pah, Enter işaretinin önünde 1 DIP ayraç kullanılır.
- **Yeni bağlantı**, **Profili düzenle**, **Bağlantıyı sına** ve **Hızlı başlangıç** ikon + metin eylemleridir. Normal durumda zemin saydamdır. Üzerine gelindiğinde satır rengi kullanılır. **Geri** düğmesi 1 DIP sınırla çizilir.
- Seçilen satır, fare üzerinden ayrıldığında da nane zeminini korur. Klavye odağı, satır ve düz eylemlerde 2 DIP kontur ve 1 DIP iç boşlukla çizilir. Dolgulu düğmenin iç odak çizgisi düğme yazısıyla aynı renktedir. İşlem sürerken eylem adı örneğin **Bağlanıyor…** olur ve yinelenen tıklama engellenir. Yüksek kontrastta yazı, seçim ve odak için Windows sistem renkleri kullanılır.

## 3. Tipografi ve ikonlar

Classic'in Windows kaynaklarında başlık, normal metin ve detay ailesi **Segoe UI** olarak tanımlı. Yeni kurallar bu aileyi sürdürür. Teknik değerler için **Consolas** seçildi. Fontu point ile alan Windows kontrollerinde `pt = DIP × 72 / 96`.

| Kullanım | Aile ve ağırlık | Boyut / satır yüksekliği (DIP) |
| --- | --- | --- |
| Ürün adı ve karşılama başlığı | Segoe UI Semibold, 600 | 16 / 20 ve 18 / 24 |
| Bölüm başlığı | Segoe UI Semibold, 600 | 16 / 20 |
| Menü, sekme, düğme ve sütun başlığı | Segoe UI 400, etkin sekme, ana eylem ve sütun başlığında Semibold 600 | 13 / 18 |
| Karşılama açıklaması, alan adı ve gövde | Segoe UI, 400 | 13 / 18 |
| Profil adı, kullanıcı, uç nokta, SQL alanı ve veri tipi | Consolas, 400 | 13 / 18 |
| Durum satırı ve kısayol ipucu | Segoe UI, 400 | 12 / 16 |

- Pencere yıldızı 20 × 20 DIP. Bölüm sekmelerinde 18 × 18, satır ve araç komutlarında 16 × 16 DIP ikon kullan. İşlev ikonları tek renkli, 1,5 DIP çizgili ve kare uçludur. Yalnız ikon gösteren kontrollerin hedefi en az 26 × 26 DIP'tir. Erişilebilir ad ve araç ipucu komutu açıklar.
- **Bağlantılar** için fiş, **Modeller** için birbirine bağlı tablolar, **Veri Taşıma** için kaynaktan çıkan ok kullan. **Yeni bağlantı** artı, **Düzenle** kalem, **Sına** yürütme üçgeni, **Hızlı başlangıç** açık kitap, karşılama denetimi yukarı/aşağı şevrondur. Sekmelerde ikonun yanında Türkçe ad görünür.
- **Profil avatarı** 20 × 20 DIP piksel çizimidir. Referansta `localhost` tek antenli nane CRT yüzü, `test` iki çıkıntılı mor vizör taşır. Avatarların parlak alanlarından örnek tonlar `#98EBA8` ve `#B989EE`. Yeni ve eski profillere ilk gösterimde bir avatar ve pastel renk atanır. Seçim profil kimliğiyle yerelde saklanır. Ad değiştiğinde ikili korunur. Profil adını avatarın hemen yanında yaz.
- Piksel avatarlarının 20, 25, 30 ve 40 fiziksel piksel sürümlerini sırasıyla %100, %125, %150 ve %200 ölçek için kullan. Keskin kenarları koru. Yıldız ve komut ikonlarında ilgili yüksek çözünürlüklü kaynak kullanılır.

## 4. Kompakt yerleşim

Başlık, tablo ve ayrıntılar aynı sol eksene oturur. Pencere içeriğinin yan marjı **12 DIP**, dar görünümde **8 DIP**. Aynı grupta 4, ikonla etiketi arasında 6, komşu kontrollerde 8, ayrı bölümler arasında en çok 12 DIP boşluk kullan. Bölümler 1 DIP çizgiyle ayrılır. Her satırın altındaki ayraç satır yüksekliğine dahildir.

| Üstten alta ana sayfa | Hedef yükseklik (DIP) | İç yerleşim |
| --- | ---: | --- |
| Ürün, uygulama menüsü ve pencere düğmeleri | 32 | Sol 12, yıldız ile ad arasında 6, menü öğeleri arasında 12. Sağda yerel Windows pencere komutları. |
| Karşılama · açık | 58 | Yatay 12, dikey 6. Başlıkla açıklama arasında 2. Sağda hızlı başlangıç ve daraltma. |
| Bölüm sekmeleri | 32 | Yatay 10, ikonla metin arası 6. Etkin sekmede 2 DIP alt çizgi ve yarı kalın yazı. |
| Arama ve yeni bağlantı | 34 | Yatay 8, dikey 4. Arama kontrolü 26 yüksekliğinde, alt çizgisi 1 DIP. Yeni bağlantı sağa yaslı. |
| Sütun başlıkları | 24 | Yatay 8, dikey ortalama. Sütun başı ve veri metni ortak eksende. |
| Profil satırı | 32 | Yatay 8, avatar ile ad arasında 8. Seçili satırın sağ ucunda **Bağlan**. |
| Seçili profil eylem şeridi | 34 | Solda 20 DIP avatar, ad ve yöntem. Ardından düzenleme ve sınama. Tablo ile şerit arasında 8 DIP. |
| Durum çubuğu | 22 | Sol 8 DIP boyunca profil sayısı, sağda Enter ipucu. Pencerenin altına sabitlenir. |

**Genişlik davranışı.** 960 DIP ve üzerinde **Profil / Kullanıcı / Sunucu / Ortam / eylem** sırası kullanılır. Eylem için 84, ortam için 80 DIP ayır. Kalan genişliği üç ana sütuna yaklaşık %30 / %28 / %42 dağıt. Altına inildiğinde ortam bilgisini seçili profil şeridine taşı. 720 DIP altında kullanıcı adı profil adının ikinci satırına geçer, kayıt yüksekliği 44 DIP olur. Sunucu ve **Bağlan** görünür kalır. Uzun değerler tek satırda üç noktayla kısalır, tam değer odakta veya araç ipucunda okunabilir. Karşılama metni, ayrıntı şeridi ve büyütülmüş metin gerektiğinde alt satıra geçer. Pencere genişlediğinde satır yüksekliği sabit kalır.

## 5. Üç ekranın davranışı

**Ortak menü.** Eski şeritte `File / Edit / View / Database / Tools / Scripting / Help` sırası ve `#F5F5F7` zemin ölçüldü. 425 × 30 px kırpımda okunur koyu yazının sol sınırları sırasıyla x = 11, 56, 101, 155, 235, 291 ve 368 px. [Eski menü ölçüm referansı](eski-menu.png)

Ana sayfa hedefi **Dosya / Çalışma alanı / Görünüm / Yardım**. **Dosya** açma ve kaydetme işlemlerini, **Çalışma alanı** profil, model ve taşıma girişlerini sunar. **Görünüm** tema ve karşılama ayarlarını, **Yardım** hızlı başlangıç ve Hakkında'yı açar. Altında **Bağlantılar / Modeller / Veri Taşıma** çalışma sekmeleri yer alır. SQL editörü gibi diğer belge bağlamlarında mevcut komut ve kısayol eşlemelerini kullan.

Menü etiketinde sağ ve sol iç boşluk 6 DIP'tir. Yan yana iki etiket arasında böylece 12 DIP kalır. Açılır menü tetikleyicinin sol kenarına hizalanır. Menü zemini genel yüzey, çevresi 1 DIP ayrımdır. Komut satırı 28 DIP, yatay iç boşluğu 8 DIP'tir. Sol ikon alanı 16 DIP, varsa kısayol sağa hizalıdır. Fare üzerindeki öğe satır vurgusunu, açılmış öğe seçim zeminini kullanır. Alt ile menü çubuğuna girilir, oklarla gezinilir, Esc önceki odağa döner.

**Bağlantılar.** İlk görünümde “StarDataParadise’a hoş geldiniz” başlığını ve “Bir sunucuya bağlanın, şemanızı tasarlayın veya verilerinizi taşıyın.” cümlesini göster. Daraltılınca şerit alanını serbest bırak ve kullanıcının tercihini sakla. **Görünüm** menüsünden yeniden açılabilir. Arama profil, kullanıcı ve sunucuyu süzer. Yukarı/aşağı okları seçimi değiştirir. Satırda Enter, çift tıklama ve **Bağlan** aynı profili açar. Seçili profil şeridinde avatar, ad, TCP/IP ve iki eylem yeterlidir. Sunucu ve port tablo hücresinde birlikte okunur. Ortam etiketi profil bilgisinden gelir. Atanmamış ortam `—` ile gösterilir. İlk kullanımda **Yeni bağlantı**, arama sonuçsuzsa **Aramayı temizle**, bağlantı başarısızsa ilgili profile bağlı hata ve **Yeniden dene** görünür.

**Modeller.** Görsel, açılmış bir şema modeli ile tablo ilişkilerini gösterir. Üst araç satırında dosya adı, **Model aç**, **Tablo** ve **İlişki ekle** bulunur. Solda başlangıç genişliği 200 DIP olan boyutlandırılabilir model ağacı, sağda diyagram vardır. Tuval ızgarası 16 DIP aralıkla ince çizilir. Tablo kutusunda başlık 24, alan satırı 22, yatay iç boşluk 8 DIP. PK ve FK yazıyla da görünür. Bağlantı çizgisi kaynak alanı hedef alana bağlar ve çokluk uçta işaretlenir. Kayıtlı model seçimi için mevcut dosya listesini kullan.

**Veri Taşıma.** Solda 176 DIP adım listesi, sağda etkin adım bulunur. Dar görünümde liste, geçerli adımın adı ve sıra numarasını taşıyan üst satıra dönüşür. Sıra **Kaynak ve hedef → Tablo eşleştirme → Önizleme → Aktarım**. Kaynak ve hedef yönü ad ve okla gösterilir. Eşleştirme satırı 30 DIP, onay kutusu 16 DIP, tıklama alanı tüm seçim hücresidir. Sütunlar **Kaynak tablo / Hedef tablo / İşlem**. Altta gerçek seçim sayısı, **Geri** ve **Önizlemeye geç** görünür. Önizleme hiçbir veri yazmaz. Aktarım mevcut sihirbazın yürütme komutuyla ayrı adımda başlar.

## 6. Classic kaynaklarındaki karşılıklar

Aşağıdaki yollar `dc3590dbfbee29c87e201d4ef53607e5deb0e4f9` sürümünün yamalı kaynak ağacına göredir. Yerel inceleme kökü `out/mysql-workbench-native-src/`. Kalıcı uygulama değişikliğinin dağıtım girdisi [8.0.47 yamasıdır](../workbench-8.0.47.patch). Hazırlama adımları [Classic derleme kılavuzunda](../README.md) yer alır.

| Yüzey | Kaynak ve bugünkü davranış |
| --- | --- |
| Üst menü | `res/wbdata/main_menu.xml` komutları tanımlar. `backend/wbprivate/workbench/wb_command_ui.cpp` bunları yükler. `frontend/windows/MySQLWorkbench/MainForm.cs` etkin belgeye ait menüyü seçer. |
| Ana bölümler ve karşılama | `backend/wbprivate/workbench/wb_context_ui_home.cpp` bağlantı ve belge bölümlerini oluşturur, taşıma için `wb.migration.open` eklentisini açar. `HomeScreen:HeadingMessage` karşılama tercihi, `HomeScreen:MigrationBannerMessage` ayrı tanıtım bandı tercihidir. Ortak kabuk çizimi `library/forms/home_screen.cpp` içindedir. |
| Liste ve yazı | `library/forms/mforms/home_screen_connections.h` kartı 241 × 91, yan boşlukları 20, ilk satır ofsetini 75 olarak tanımlar. `library/forms/home_screen_connections.cpp` çizimi, aramayı ve isabet testini yönetir. Kompakt listeye geçerken fare, klavye ve erişilebilirlik sınırları da aynı satır geometrisini kullanmalıdır. Windows yazı ailesi `library/forms/home_screen_helpers.cpp` içindedir. |
| Marka ve gezinme görselleri | `images/icons/MySQLWorkbench-128.png` mevcut StarDataParadise yıldızıdır. Eski bölüm ikonları `images/home/sidebar_wb.png`, `sidebar_modeling.png` ve `sidebar_migration.png` ailesindedir. Yeni piksel profil avatarlarının çizim referansı Bağlantılar PNG'sidir. |
| Model ve taşıma çalışma alanları | `library/forms/home_screen_documents.cpp` model girişlerini, `frontend/windows/MySQLWorkbench/ModelDiagramForm.cs` diyagramı taşır. `plugins/migration/frontend/` altındaki `migration_source_selection.py`, `migration_object_selection.py`, `migration_object_editing.py` ve `migration_data_transfer.py` taşıma akışının kaynaklarıdır. |

## 7. Kabul ve doğrulama

- Görsel pilotu üç referans durumuyla karşılaştır. 1366 × 768 ve 1920 × 1080 pencerelerde %100, %125, %150 ve %200 DPI kullan. Ekran genişliği ve DPI'ı ayrı kaydet. Dar pencere, uzun Türkçe ad, sıfır kayıt ve çok kayıt durumlarını da dene.
- Klavyeyle menüyü aç, bölümler arasında geç, aramayı daralt, iki profilden birini seç ve Enter ile aç. Aynı uç noktadaki iki kullanıcı anlaşılır kalmalı. Yeniden başlatınca profil avatarı ile renk eşleşmesi korunmalı.
- Açık ve koyu yüzeylerde normal yazı için en az 4,5:1, etkileşim için gerekli çizgi ve odak göstergesinde en az 3:1 kontrast hedefle. Windows yüksek kontrastta ve büyütülmüş metinde seçim, hata ve odak ayrıca okunabilmeli.
- Modelde anahtar alanları ve ilişki ucu, taşımada kaynak/hedef yönü ve seçili tablo sayısı denetlenir. Canlı uygulama kabulü, Classic Windows derlemesi ve bağlantı/taşıma senaryoları için uygun test sunucusu gerektirir.

**Belge doğrulaması · 18 Kasım 2026.** Dört PNG'nin boyutları ve özgün dosyalarıyla SHA-256 eşitliği, belge bağlantıları ve belirtilen Classic kaynak yolları yerelde denetlendi. Segoe UI, Segoe UI Semibold ve Consolas font dosyaları bu Windows kurulumunda bulundu. Python ortamında Pillow bulunmadığı için piksel ölçümleri `System.Drawing.Bitmap` ile alındı. Açık/koyu tema paletinde sınanan 40 renk çiftinde en düşük normal metin kontrastı 4,70:1, en düşük sekme göstergesi kontrastı 3,74:1 çıktı. Üstteki etkileşim ve DPI senaryoları uygulama pilotunda çalıştırılacaktır.
