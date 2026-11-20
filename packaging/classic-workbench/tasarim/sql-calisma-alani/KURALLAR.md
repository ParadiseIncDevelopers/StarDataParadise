# Classic çalışma alanı tasarım kuralları

19 Kasım 2026. Bu belge bağlı SQL oturumunun çalışma alanını, menülerini, araç çubuklarını ve bunlardan açılan sayfa ailelerini kapsar. [41 sayfalık galeri](README.md), [sayfa davranışları](SAYFALAR.md) ve [komut envanteri](KOMUTLAR.md) birlikte okunur. Kullanıcının son düzeltmesi aşağıdaki başlık sırasını ve düz kenar kuralını bütün görsellerin üzerinde geçerli kılar.

## Seçilen tasarımlar

- Ana belge/bağlantı sekmeleri için [3. taslak](referanslar/onayli-sekmeler-3.png) seçildi. Petrol şerit, sabit ana sayfa, düz nane etkin sekme ve profil avatarı kullanılır.
- Bağlantı beklemesi için [1. taslak](referanslar/onayli-baglanti-beklemesi-1.png) seçildi. Seçilen profil satırı bekleme durumunu ve Vazgeç eylemini gösterir.
- Yeni bağlantı, profil yönetimi ve Veri Taşıma için önceki [tasarım ve uygulama kaydı](../baglanti-ve-tasima/UYGULAMA.md) referanstır.
- Help menüsündeki dış bağlantı hedefleri, kullanıcının son isteğiyle sonraki aşamaya bırakıldı.

Galerideki sayfalar yerleşim ve etkileşim referanslarıdır. Görsellerin metinleri, verileri, grafik değerleri, dosya yolları ve sürüm ayrıntıları temsili içeriktir. Çalışan uygulamanın kaynak, derleme ve kabul kapsamı [uygulama kaydındadır](UYGULAMA.md). Kesin ölçüler aşağıdaki DIP değerlerinden, SQL ve veri ise mevcut native üretici ve modellerden gelir. Görsellerdeki kısayol ipuçları için mevcut komut kısayolu tablosu esas alınır.

## Pencere ve gezinme

| Katman | İçerik | Yükseklik |
| --- | --- | --- |
| Uygulama başlığı | Ürün yıldızı/adı ve Windows pencere komutları | Native Windows ölçüsü |
| Ana belge şeridi | Sabit ana sayfa, Veri Taşıma, bağlantı/model belgeleri | 32 DIP |
| Menü | Dosya, Düzenle, Görünüm ve etkin belgenin mevcut komut grupları | 26 DIP, fonta göre esnek |
| Etkin bağlantının alanları | SQL, Veri, Şema, Performans, Yönetim, Araçlar | 32 DIP |
| Sayfanın komut satırı | Etkin sayfaya ait komutlar ve seçimler | 30–34 DIP |
| İçerik | Nesne ağacı, düzenleyici, tablo, önizleme ve gerektiğinde ayrıntı çekmecesi | Esnek |
| İşleme bağlı alt satır | Bekleyen düzenleme, aktarma eylemi veya sonuç özeti | 30–34 DIP |

Uygulama menüsü dosya ve pencere işlemlerini yönetir. Bağlantı alanları etkin oturumdaki görevi değiştirir. SQL tamponları aynı bağlantı altında belge olarak açılır. Yönetim sayfalarına geçildiğinde SQL tamponu, seçimi ve taslağı korunur. Komutların hedefi etkin belge ve bağlantı nesnesinden alınır.

| Alan | İçerik |
| --- | --- |
| SQL | SQL tamponları, sonuçlar, mesajlar, sorgu planı, yerel sorgu geçmişi, parçalar ve bağlamsal yardım |
| Veri | Tablo verisi, filtre, hücre ayrıntısı, tablolarda arama, CSV/JSON giriş ve çıkışı |
| Şema | Metadata, nesne tanımları, DDL, tablo bakımı, sunucudan model çıkarma, şema aktarımı |
| Performans | Canlı izleme, sorgu raporları, Performance Schema ölçüm ayarları |
| Yönetim | Genel bakış, Oturumlar, Kullanıcılar, Değişkenler, Dışa aktar, İçe aktar, Hizmet, Günlükler, Yapılandırma |
| Araçlar | Betikler/eklentiler ve kurulumu doğrulanan StarDataParadise araçlarının girişleri |

Ana sayfadaki EER girişinin önerilen etiketi **Şema modelleri**, makine öğrenmesi girişinin etiketi **CPU modelleri** olur. Bu adlar ilgili veri kaynağını açıklar. Tür eşleştirme, Veri Taşıma araçları içinden açılır. Harici yardımcı programın yolu kurulum ayarından çözülür.

## Renk ve tipografi

İki ayrı başlık veya bilgi parçası ` - ` ile ayrılır. Görsel metin ayıracı olarak nokta veya orta nokta kullanılmaz.

Önceki [ana sayfa paleti](../ANA_SAYFA.md) sürdürülür.

| Rol | sRGB |
| --- | --- |
| Genel yüzey | `#EDF2F4` |
| Araç ve başlık yüzeyi | `#E7EEEF` |
| Düzenleyici/sonuç yüzeyi | `#FFFFFF` |
| Ana yazı ve komut ikonu | `#102629` |
| İkincil yazı | `#4F5759` |
| İnce ayraç | `#A3B4BC` |
| Seçili satır ve etkin ana sekme | `#DDEBE4` |
| Etkin alt çizgi | `#098874` |
| Ana sekme şeridi ve birincil eylem | `#043133` |
| Birincil eylem yazısı | `#FFFFFF` |
| Bekleyen değişiklik yüzeyi | Açık amber, koyu yazı ve hücre işaretiyle birlikte |

| Kullanım | Font | Boyut |
| --- | --- | --- |
| Menü, sekme, alan, düğme, tablo | Segoe UI | 13 DIP, 9,75 pt |
| Etkin sekme ve sütun başlığı | Segoe UI Semibold | 13 DIP |
| Sayfa başlığı | Segoe UI Semibold | 16 DIP |
| SQL, hex, ad, uç nokta, hassas sayı | Consolas | 13 DIP |
| Yardımcı metin | Segoe UI | 12 DIP |

Font ölçümü, çizimi ve isabet alanı aynı DPI dönüşümünü kullanır. Etkin sekmenin yarı kalın ölçüsü önceden ayrılır. Türkçe karakterler tek kod noktası dizisi olarak korunur. GDI ve native metin ölçümü arasında farklı genişlik hesabı kullanılmaz.

## Boşluk ve kontrol ölçüleri

| Bileşen | Ölçü |
| --- | --- |
| İçerik kenarı | 12 DIP, dar görünümde 8 DIP |
| Aynı grupta aralık | 4–6 DIP |
| Komşu kontroller | 8 DIP |
| Ayrı bölüm aralığı | 12 DIP |
| Düğme/metin girişi | En az 26 DIP yüksekliğinde, metin büyümesine göre esnek |
| Düğme yatay iç boşluğu | 10 DIP |
| İkon ve yazı arası | 6 DIP |
| İkon | 16 DIP, bölüm ikonunda 18 DIP |
| Yalnız ikon hedefi | En az 26×26 DIP |
| Sol gezgin | Başlangıç 220 DIP, boyutlandırılabilir |
| Sağ ayrıntı/yardım çekmecesi | Başlangıç 280 DIP, açılıp kapanabilir |
| Tablo satırı | 28 DIP, çok satırlı içerikte esnek |
| Sütun başlığı | 26 DIP |
| İnce ayraç | 1 DIP |
| Etkin alt sekme çizgisi | 2 DIP |
| Köşe yarıçapı | 0 DIP, düz dikdörtgen yüzeyler |

Tablo hücreleri 8 DIP yatay iç boşluk taşır. Sayısal değerler sağa, metin sola hizalanır. Tam değer odakta veya araç ipucunda okunur. Başlık ve veri ortak sütun eksenindedir. Düzenlenen hücre, renge ek olarak köşe işareti ve bekleyen değişiklik özetiyle belirlenir.

960×600 DIP çalışma alanında ana görev erişilebilir kalır. Yardım ve ayrıntı çekmeceleri dar görünümde kapanabilir. Araç çubuğu ikincil eylemleri açılır menüde toplanır. Ana eylem ve seçili nesne korunur. İçerik kaydırılırken aktarım/inceleme alt eylemleri görünür kalır. Metin büyütme, satır ve kontrol yüksekliğini içerikten üretir.

Kalın renkli kenar şeridi, renkli tek kenarla çevrelenen bilgi/uyarı kartı ve kıvrımlı panel çerçevesi kullanılmaz. Uyarı; simge, kısa metin ve gerekirse ince nötr ayraçla gösterilir. Görsellerdeki eski amber sol kenar şeridi uygulanmaz. Seçim için düz yüzey ve ince alt çizgi kullanılabilir.

## Ana sekme şeridi

Ana sayfa sabittir. Belge sekmesinde tür ikonu veya profil avatarı, ad ve kapatma eylemi bulunur. Aktif bağlantıda kayıtlı `sdpHomeAvatar` değeri kullanılır. `test` için çizilen mor avatar görsel örnektir. Profil kimliğiyle eşleşen gerçek avatar bütün girişlerde aynıdır.

Sekme yüksekliği 32 DIP, avatar 16–20 DIP, ikon/yazı arası 6 DIP, kapatma hedefi 26 DIP olur. Etkin sekme şerit içinde 3 DIP üst/alt boşlukla oturur. Uzun adlar kısaltılır ve tam ad araç ipucunda gösterilir. Çok sekmede yatay kaydırma veya açık belge listesi kullanılır. Mevcut yeniden sıralama, belgeyi kapatma ve kaydetme akışları native komutlarına gider.

Klavye odağı ayrıca çizilir. Bağlantı kopukluğu ve kaydedilmemiş tampon işareti metin/ikonla anlaşılır. Üretim/test etiketi profil metadata’sı varsa gösterilir.

## Bağlantı beklemesi

Seçilen satır `Bağlanıyor…` ve **Vazgeç** durumuna geçer. Avatar ve profil adı yerinde durur. Aktivite göstergesi belirsiz ilerleme kullanır. TCP, TLS ve kimlik doğrulama alt aşamaları yalnız gerçek geri bildirim varsa adlandırılır.

Her denemenin kimliği vardır. Tekrarlanan tıklama aynı denemeyi çoğaltmaz. İptal, SSH/TLS/parola istemi, hata ve pencere kapanışı native bağlantı yaşam döngüsüyle eşleşir. Geç dönen sonuç daha yeni denemenin veya kapatılmış pencerenin durumunu değiştirmez. Başarılı sonuç SQL sekmesini açar. Hata ilgili profile bağlı metin ve yeniden deneme eylemiyle gösterilir.

Kaynaktaki `create_connected_editor`, `getTunnel`, `run_cancelable_task` ve `cancel_connect_editor` davranışları satır durumuna uyarlandı. Bekleme döngüsü UI olaylarını işlerken yeni açma ve kapanışı denetler; iptalden sonra tamamlanan sonuç serbest bırakılır. İptal işbirliklidir ve mevcut bağlayıcı zaman aşımını bekleyebilir. Native derleme ve başarılı canlı TLS bağlantısı doğrulandı; bütün SSH/iptal yarışlarının canlı kabulü yapılmadı. Ayrıntılar [uygulama kaydındadır](UYGULAMA.md).

## Komut davranışları

Çalıştır açılır menüsü tüm betik, seçim ve imleçteki ifade kiplerini açıkça adlandırır. **Durdur** yalnız etkin yürütmede açıktır. Autocommit, Commit ve Rollback bağlantı düzeyindedir. Sonuç hücresini taslakta değiştirme ile sunucuya uygulama farklı durumlardır.

Nesne oluşturma eylemleri **Oluştur** menüsünde şema, tablo, görünüm, yordam ve fonksiyon olarak bulunur. Oluşturma ve düzenleme aynı form ailesini paylaşır. İnceleyici nesne metadata’sını okur. SQL önizlemesi native üreticinin çıktısını gösterir. Uygulama sonucunda her ifadenin başarı/hata durumu korunur.

Dosya seçicileri, SQL açık/kaydet, kopyalama, biçimlendirme ve panel açma eylemleri mevcut komut işleyicilerini kullanır. [Komut tablosu](KOMUTLAR.md) her girişin hedefini verir. Yeni arama, çekmece, özet ve önizleme bağları aynı veri modellerinden beslenir.

Satır düzenleme, yetki değişikliği ve canlı değişken ayarı önizlemeleri kullanıcıya önerilen etkileşimlerdir. Bunların native davranışa nasıl bağlanacağı seçilen sayfa tasarımının uygulama aşamasında belirlenir. Mevcut işleyiciler ve hata davranışları başlangıç kaynağıdır.

## Özel sayfa ayrıntıları

- Tablo bayrakları bağımsızdır. Üretilmiş sütun seçilince ifade ve Virtual/Stored alanları açılır. Normal sütunun varsayılan değeri ayrı alandır.
- Sayaçlar okunan değerleri gösterir. Sistem değişkenlerinde scope ve dinamik düzenlenebilirlik okunur. Seçenek dosyası kalıcı dosya değişikliğini ayrıca gösterir.
- Performance Schema `ENABLED` ve `TIMED` alanları ayrı boolean kontrollerdir. Desteklenmeyen zaman ölçümü için açıklama ve devre dışı durum kullanılır.
- İşlem ve bağlantı sonlandırma farklı eylemlerdir. Seçim yenileme sırasında işlem kimliğiyle korunur.
- Hizmet adı, dosya yolu ve yönetim yöntemi profil kaydından gelir. Eksik ayar sayfasındaki eylem doğrudan profil yöneticisinin ilgili bölümünü açar.
- İçe aktarma önizlemesinde betiğin kendi şemasını seçebileceği görünür. CSV/JSON alan eşleştirmesi ve SQL dump farklı araçları çağırır.
- BLOB görüntüleyici uzunluğu ve ham baytları korur. Metin kodlaması bir görüntüleme seçeneğidir. NULL ve boş değer ayrı durum taşır.
- Görseldeki SQL ve Python örnekleri yerleşimi açıklar. SQL, bakım, Decision ve History çıktısı uygulamada gerçek üretici/sonuç nesnesinden alınır.

## StarDataParadise araçları

32–36 sayfaları gerçek Classic adaptörlerine bağlandı. Kurulu yordamlar, UDF/worker, günlük ve araç yolları okunarak uygun girişler sunulur. Kullanıcı yetkisi, araç çıktısı ve hata durumu ilgili sağlayıcının mevcut sözleşmesinden gelir. Desteklenen eylemler, testler ve çalışma zamanı sınırları [araçlar kaydındadır](ARACLAR.md).

| Yüzey | Kullanılacak mevcut kaynak | UI bağlama ihtiyacı |
| --- | --- | --- |
| Veri geçmişi | `compatibility/history` LOG/READ/DIFF ve tek mevcut satır restore/revert | Günlük seçimi, revizyon sayfalama, fark ve plan/sonuç bağlama |
| Decision | `compatibility/decision/provision.py` | Dosya, tüm kural seti, çeviri hatası, üretilen SQL ve inceleme |
| CPU modelleri | `compatibility/ml` ve Classic model yordamları | Sürüm listesi, özellik sırası, eğitim/tahmin sonuçları ve lifecycle |
| Compute işleri | `compatibility/jobs/compute_jobs.py` | Kuyruk/schedule kayıtları, UTC gösterimi, worker/iptal durumu |
| Doğrulanmış yedekler | `compatibility/backup` | Arşiv sağlayıcısı, manifest, doğrulama ve hedefe göre geri yükleme |

Eski Qt/wire-v1 etap belgelerindeki kapsam ile güncel Classic uyumluluk araçlarının sınırları ayrı kaynak kayıtlarıdır. Bu plan güncel `compatibility/` belgelerini esas alır. Eklentiye bağlı Audit/Firewall/Enterprise girişleri kaynak envanterinde işaretlenmiştir. Ürün yedek araçları kendi sağlayıcısıyla bağlanır.

## Uygulama sırası

1. Seçilen ana sekme şeridi ve satırda bağlantı beklemesi.
2. SQL çalışma alanı, sonuç kümesi, mesajlar, geçmiş ve yardımcı çekmeceler.
3. Şema inceleyici ve nesne editörleri, ortak SQL incelemesi.
4. Yönetim, performans, dosya aktarımı ve yardımcı araçlar.
5. Seçilen StarDataParadise adaptörleri ve ilgili kurulum durumları.

Her uygulama diliminde native giriş, ilgili kaynak modeli, hata/iptal durumu, klavye erişimi ve gerçek çıktı kontrol edilir. Tekrarlanan aynı tür alanlar ortak bileşenle çizilir. Tablo, görünüm, yordam ve fonksiyon tiplerinin kendi metadata ve yazma davranışları korunur.
