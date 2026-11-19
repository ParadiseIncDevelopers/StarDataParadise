# Canlı inceleme kaydı

19 Kasım 2026. Windows Classic Release_OSS x64. Kaynak HEAD `dc3590dbfbee29c87e201d4ef53607e5deb0e4f9` ve mevcut yerel yama incelendi. %125 masaüstü ölçeğinde 1482×970 ve büyütülmüş 1920×1020 görüntüler kullanıldı.

[Galeri](README.md) · [Komut eşleştirmeleri](KOMUTLAR.md)

24 ekran kaydı bu çalışmada alındı. Şema ve tablo bağlam menülerinin metin ağaçları da kaydedildi. Sayfa 37–41 ve beş entegrasyon önerisinin alanları ayrıca kaynaktan çıkarıldı.

## Gözlenen akış

### 1. SQL başlangıcı

**İyileştirme gerekli.** Çalışma alanının büyük bölümü yardımın kapalı durum metnine ayrılıyor. SQL komutları iki araç çubuğunda dağılmış. 01,05,06 taslakları bu yerleşimi ele alıyor.

![SQL başlangıcı](referanslar/mevcut/01-sql-editor.jpg)

### 2. Sunucu menüsü

**İyileştirme gerekli.** Temel yönetim ile eklentiye bağlı kurumsal girişler aynı listede. Menüdeki komutlar kaynak kayıtlarıyla eşleştirildi.

![Sunucu menüsü](referanslar/mevcut/02-server-menu.jpg)

### 3. Sunucu durumu

**Açıldı.** Sürüm, durum, bağlantılar ve kaynaklar okundu. Eski MySQL görseli ve geniş yüzeyler görülüyor. 12 taslağı bu içeriği yeniden yerleştiriyor.

![Sunucu durumu](referanslar/mevcut/03-server-status.jpg)

### 4. İstemci oturumları

**Açıldı.** İşlem tablosu, yenileme seçimi, uyuyanları gizleme ve iki ayrı iptal/sonlandırma eylemi var. Sonlandırma işlemi denenmedi.

![İstemci oturumları](referanslar/mevcut/04-client-connections.jpg)

### 5. Kullanıcılar ve yetkiler

**Açıldı.** Hesap listesi ve dört alt sekme gözlendi. Hesap ve yetki değişikliği yapılmadı. Uzun açıklamalar kompakt formda yeniden düzenleniyor.

![Kullanıcılar ve yetkiler](referanslar/mevcut/05-users-privileges.jpg)

### 6. Sayaçlar ve değişkenler

**Açıldı.** Tümü kategorisi seçilince veriler göründü. Kategori seçilmemiş ilk görünüm boş kalıyor. 15 taslağında varsayılan kategori açıkça seçili.

![Sayaçlar ve değişkenler](referanslar/mevcut/06-status-variables.jpg)

### 7. SQL dışa aktarma

**Açıldı.** Şema/tablo seçimi, dump seçenekleri ve dosya/klasör modları gözlendi. Çıktı alanı sayfanın alt seçeneklerini daraltıyor.

![SQL dışa aktarma](referanslar/mevcut/07-data-export.jpg)

### 8. SQL içe aktarma

**Açıldı.** Dosya/klasör ve hedef şema alanları gözlendi. Aktarım başlatılmadı. Hedef özeti 20 taslağında açıkça gösteriliyor.

![SQL içe aktarma](referanslar/mevcut/08-data-import.jpg)

### 9. Hizmet yönetimi

**Önkoşul sorunu.** Kayıtlı hizmet adı boşken MySQL adı varsayıldığını söyleyen günlük görüldü. Başlat/durdur denenmedi. 21 taslağı profil eşleştirme durumunu gösteriyor.

![Hizmet yönetimi](referanslar/mevcut/09-service-control.jpg)

### 10. Sunucu günlükleri

**Yapılandırma yolu gerekli.** Sayfa, sunucu seçenek dosyası yolu eksik mesajında kaldı. Günlük içeriği için native okuyucu ve kontroller kaynakta incelendi.

![Sunucu günlükleri](referanslar/mevcut/10-logs-config-required.jpg)

### 11. Seçenek dosyası

**Yapılandırma yolu gerekli.** Dosya yolu olmadığı için form açılamadı. İçerik ve kategori yapısı kaynaktan incelendi.

![Seçenek dosyası](referanslar/mevcut/11-options-config-required.jpg)

### 12. Canlı performans

**Açıldı.** Ağ, SQL ve InnoDB ölçümleri gözlendi. Grafik ve halka yoğunluğu 16 taslağında üç zaman serisi alanına dönüştürülüyor.

![Canlı performans](referanslar/mevcut/12-performance-dashboard.jpg)

### 13. Performans raporları

**Açıldı.** Statement Analysis raporu gerçek verilerle yüklendi. Çok geniş sütunlar ve tekrar eden alan başlıkları 17 taslağında düzenleniyor.

![Performans raporları](referanslar/mevcut/13-performance-reports.jpg)

### 14. Ölçüm ayarları

**İçerik doğrulanamadı.** Sayfa başlığından sonra içerik boş kaldı. Bunun nedeni bu incelemede belirlenmedi. 18 taslağının alanları native instrumentation kaynağından alındı.

![Ölçüm ayarları](referanslar/mevcut/14-performance-setup-empty.jpg)

### 15. Şema inceleyicisi

**Açıldı.** Genel, tablo, sütun, indeks, tetikleyici, görünüm, yordam, fonksiyon, yetki ve event alt yüzeyleri görüldü.

![Şema inceleyicisi](referanslar/mevcut/16-schema-inspector.jpg)

### 16. Tablo inceleyicisi

**Açıldı.** Metadata ve tahmini satır/boyut alanları okundu. ANALYZE eylemi çalıştırılmadı.

![Tablo inceleyicisi](referanslar/mevcut/18-table-inspector.jpg)

### 17. Tablo tanımı

**Açıldı.** Üç sütun, bağımsız bayraklar ve sütun/indeks/FK/tetikleyici/bölümleme/seçenek sekmeleri gözlendi. Tanım değiştirilmedi.

![Tablo tanımı](referanslar/mevcut/19-table-editor.jpg)

### 18. Veri araması

**Açıldı.** Eşleme yöntemi, iki ayrı satır sınırı ve bütün türleri arama seçeneği var. Veri araması başlatılmadı.

![Veri araması](referanslar/mevcut/20-data-search.jpg)

### 19. Tercihler

**Açıldı.** Kategori ağacı ve düzenleyici alanları gözlendi. Cancel ile kapatıldı. 24 taslağı ortak form ailesini gösteriyor.

![Tercihler](referanslar/mevcut/21-preferences.jpg)

### 20. Yerel sorgu geçmişi

**Açıldı.** Tarih listesi ve SQL kayıt alanı gözlendi. Bu geçmiş ile sunucudaki veri revizyonları ayrı veri kaynaklarına sahip.

![Yerel sorgu geçmişi](referanslar/mevcut/22-query-history.jpg)

### 21. Python/GRT betik alanı

**Açıldı.** Betik dosyaları, modül ağacı ve Python konsolu görüldü. Betik çalıştırılmadı. Pencere kapatıldı.

![Python/GRT betik alanı](referanslar/mevcut/23-scripting.jpg)

### 22. Sonuç kümesi

**Salt okunur sorgu geçti.** Yalnız sabitlerden oluşan SELECT/UNION sorgusu iki örnek satır döndürdü. Sonuç araçları ve salt okunur işareti gözlendi.

![Sonuç kümesi](referanslar/mevcut/24-query-results.jpg)

### 23. Plan görünümü

**EXPLAIN geçti.** Aynı salt okunur ifade için plan sonucu geldi. Yeni SQL tamponu inceleme örneği olarak oturumda kaldı.

![Plan görünümü](referanslar/mevcut/25-explain.jpg)

### 24. SQL parçaları

**Boş durum açıldı.** Kategori seçicisi ve ekle/değiştir komutları gözlendi. Mevcut kütüphane boş göründü. Yeni parça kaydedilmedi.

![SQL parçaları](referanslar/mevcut/26-snippets.jpg)

## Erişilebilirlik ve doğrulama sınırları

Görüntüler dar tıklama hedefleri, yalnız ikonla sunulan komutlar, yüksek yazı boyutu farkı ve boş yardımcı panellerin alan tüketimini gösteriyor. Yeni taslaklar ikon yanında gerekli metni, seçimi renk yanında yazı/çizgiyle ve odak görünürlüğünü öngörüyor. Ekran okuyucu uyumluluğu, klavye ile tüm sayfa dolaşımı, yüksek kontrast ve tam DPI matrisi bu tasarım incelemesinde kabul testinden geçirilmedi.

Bu etapta derleme çalıştırılmadı. Üretilenler görsel taslaklar, davranış eşleştirmeleri ve yerleşim kurallarıdır. Canlı sunucuda kontrol edilen yürütmeler sabit SELECT ve EXPLAIN ile sınırlıdır. Önceki bağlantı ve taşıma kabul kayıtları kendi etap belgelerinde bulunur.

İlk pencere konum değişimi ve güncel olmayan erişilebilirlik indeksleriyle girişler yeniden gözlem alınarak düzeltildi. Bir görüntü Codex penceresini gösterdiği için kanıt olarak kaydedilmedi. Sonraki hedef seçimi ve büyütülmüş görünümle inceleme sürdü. İki günlük sayfasının eksik ayarı ve boş ölçüm sayfası yukarıda ayrıca belirtilmiştir.

Help dış bağlantı hedefleri kullanıcının son isteğiyle ertelendi. Bu durum bir erişim hatası olarak raporlanmaz.
