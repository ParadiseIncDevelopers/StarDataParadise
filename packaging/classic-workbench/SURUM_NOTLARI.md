# Classic sürüm notları

## v0.3.0 - AI ve Knowledge çalışma alanı

Ürün sürümü 0.3.0, kanal stable olur. Classic kaynak tabanı 8.0.47'dir.

- Knowledge parametreleri ve sonuçları, bağımlılık diyagramı ve sürüm timeline'ı aynı native çalışma alanında bulunur.
- Konuşmalar, Behavior, yanıt şemaları, AI modelleri, SQL/PLSQL, yetkiler/onay ve ayrı AI SQL yürütme ekranı eklenir. Konuşma formunun ardından SQL düzenlenip kullanıcı tarafından çalıştırılır.
- Model seçenekleri OpenAI, Claude/Anthropic ve kullanıcının içe aktardığı yerel GGUF modelidir. Credential Manager kaydı ile worker erişiminin doğrulanması ayrı gösterilir.
- Parametreli Knowledge/araç çağrıları, onay önizlemesi ve eylem kimliğine bağlı onay/ret/yürütme SQL'i hazırlanır. Görülen eylem değişirse eski önizleme geçersizleşir.
- Normal SQL/PLSQL kısa biçimleri için capability tabanlı tamamlama, tür bilgisi ve hover eklenir. Compact ayrı bir SQL modu değildir.
- Katalog sorgularının çoklu sonuçları ve son OK yanıtı tamamıyla tüketilir. Yenileme sırasında ana bağlantı ve etkin rol korunur; ilk buffered sonuç okunabilir kalır ve sonraki SQL hataları kaybolmaz.
- Modal işlem düğmeleri ana koyu yeşil `#043133` rengi ve beyaz metin kullanır. Başlık ve parametre ayıracı ` - ` olur.

HIST/Git ve yeni SQL JOB komutları/ekranları sonraki etaba ertelendi. Önceki tasarım görselleri gelecekteki çalışma için korunur. Ayrıntılı gerçek test sonuçları ve çalıştırılmayan dış sağlayıcı kontrolleri [ortak kabul kaydındadır](https://github.com/ParadiseIncDevelopers/StarDataParadiseServer/blob/v0.5.0/docs/coordination/STABLE_RELEASE_2026_11_21.md).

## v0.3.0-preview.5 - Classic Birleşik Çalışma Alanı

Tarih: 20 Kasım 2026. Dal: `master`. Bu kaynak önizlemesinin amacı, onaylanan arayüz tasarımlarını çalışan native Classic ekranlarına uygulamak ve veri düzenleme/onay sınırlarını canlı doğrulamaktır. Classic kaynak tabanı 8.0.47 olarak korunur; bu etiket ürün deposunun önizleme sürümüdür.

| Değişiklik | Amaç |
| --- | --- |
| Ortak uygulama kabuğu, ana sekmeler ve SQL dosya sekmeleri | Başlık, ana sekmeler, menü ve çalışma alanlarını ortak sıraya almak; taşan sonuç başlıklarını okunur tutmak. |
| Profil avatarları ve satır içinde bağlantı beklemesi | Bağlantıları ayırt etmek; bağlantı açılışı ve iptalini aynı yüzeyden yönetmek. |
| Kompakt Şema Gezgini, ortak ikonlar ve Çalıştır araçları | Nesne bulmayı, SQL yürütmeyi ve panel geçişlerini tutarlı hale getirmek. |
| Veri gridine bağlı sağ satır formu | Aynı Recordset'in seçili kaydını grid ve formda eşzamanlı göstermek; NULL ile boş metni ayırmak. |
| Native tablo/şema düzenleyicisi ve metadata inceleyicisi | Sütunları, indeksleri, bayrakları ve gerçek salt okunur DDL'yi aynı çalışma alanında incelemek. |
| Ortak SQL önizleme ve onay akışı | Satır/DDL değişikliklerini uygulamadan önce göstermek; Vazgeç/Geri al işlemlerinde sunucu verisini korumak. |
| Sonuç alt sekmeleri ve sorgu planı | Tablo, Form, Sütun türleri, Sorgu ölçümleri ve Ağaç/Tablo/JSON planlarını gerçek sonuç üzerinden açmak. |
| Türkçe/İngilizce ortak dil paketi | Native, Python ve WinForms metinlerini kayıtlı dil seçimine bağlamak; ham SQL/veri/nesne adlarını korumak. |
| Yönetim, performans ve SDP SQL ölçümleri | Eski marka yüzeylerini ortak ikonlarla yenilemek; model/iş/geçmiş/TLS ve sunucu sayaçlarını gerçek kaynaklarından göstermek. |
| Değişkenler açılışı ve kopyalama/arama eylemleri | Yanlış TreeView argümanını düzeltmek; durum ve sistem değişkenlerini istisnasız açmak. |
| Hata penceresi ve ürün araçları | Türkçe hata özeti/teknik ayrıntı sunmak; History, Decision, model, Compute ve yedek araçlarını native dock sayfalarına bağlamak. |
| Derleme, test ve tasarım kaynakları | Kanonik kaynakları hash/çatışma denetimiyle sahnelemek; ikon/SVG lisanslarını, testleri, taslakları ve canlı kabul görsellerini kaynakla birlikte sürümlemek. |

### Doğrulama

- 18 native hedefin tam derlemesi ve sonraki hedefli derlemeler geçti; son tur `WBControls` ve `wbprivate` hedeflerini içerir.
- Yedi Python grubunda 66 sınama; C++ kabulünde 5+5, C# kaydırma kabulünde 11 kontrol geçti.
- Kaynak denetiminin dokuz ve çalışma zamanı denetiminin yedi grubu geçti; 340 kanonik kaynak, dört dil kataloğu ve 18 x64 PE doğrulandı.
- Canlı satır iptali 20.00 değerini korudu; yalnız geçici test tablosundaki onaylı kayıt 24.75 oldu. DDL önizlemesinden vazgeçme tablo tanımını korudu.
- İşe ait geçici QA şeması temizlendi ve yokluğu doğrulandı. Mevcut kullanıcı tablolarına yazma yapılmadı.
- Son kabulde 21 native ekran görüntüsü, paket denetiminin 18 kontrolü ve 414 yerel belge bağlantısı doğrulandı.

[Ayrıntılı canlı kabul ve ekranlar](tasarim/sql-calisma-alani/VERI-SEMA-YONETIM.md) - [Uygulama ve önceki etaplar](tasarim/sql-calisma-alani/UYGULAMA.md) - [Ürün araçları](tasarim/sql-calisma-alani/ARACLAR.md)

`out/` altındaki derleme çalışma zamanları, bağımlılıklar, yerel kullanıcı ayarları ve geçici test verileri deponun ignore kurallarıyla yerelde kalır. MSI kur/kaldır, tam DPI/yüksek kontrast/ekran okuyucu matrisi ve canlı SSH/GDAL kabulü ayrı kapsamdır. Yardım dış bağlantıları önceki kullanıcı kararıyla ertelenmiştir.

## v0.3.0-preview.4 - Tam Classic Kaynakları ve Bağlantı Akışları

İlk tam Classic kaynak ağacını `master` altında yayımlar. Kompakt ana sayfa, Bağlantılar/Modeller/Veri Taşıma, TLS/SSH profil alanları ve veri taşıma SQL önizlemesi bu tabanı oluşturur. [Bağlantı ve taşıma kabulü](tasarim/baglanti-ve-tasima/UYGULAMA.md)
