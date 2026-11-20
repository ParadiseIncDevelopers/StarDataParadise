# StarDataParadise Classic çalışma alanı taslakları

Bağlı SQL çalışma alanı ve buradan açılan yüzeyler için 41 ayrı sayfa/durum taslağı. 19 Kasım 2026.

**Çalışan Classic uygulaması:** [uygulama kaydı](UYGULAMA.md), [tamamlanan Veri/Şema ve yönetim kabulü](VERI-SEMA-YONETIM.md), [beş ürün aracının kapsamı](ARACLAR.md). Güncel çıktı `out/classic-data-schema-delivery/StarDataParadise.exe`; derleme [Build-Workspace.ps1](../../Build-Workspace.ps1) ile yapılır. Derlemeler, 66 Python sınaması, 21 derlenmiş kontrol, Türkçe/İngilizce yeniden açılış, veri/DDL akışları ve QA temizliği geçti. [21 son native görüntü](VERI-SEMA-YONETIM.md#canli-kabul-goruntuleri) gerçek kabul kanıtıdır. Aşağıdaki 41 görsel tasarım referanslarıdır.

[Son çalışan Veri ekranı](uygulama/veri-sema-yonetim/14-son-uygulama.jpg), gerçek tablonun sekiz satırını ve seçili sekizinci kaydı gösterir. [Önceki görsel uyum etabı](UYGULAMA.md#gorsel-uyum) ve [önceki SQL ekranı](uygulama/fidelity/sql-final.jpg) tarihsel kabul kaydında korunur.

**Seçilen yön** - [Sekmeler 3](referanslar/onayli-sekmeler-3.png) ve [bağlantı beklemesi 1](referanslar/onayli-baglanti-beklemesi-1.png). Yeni sayfalarda aynı açık tema, profil avatarı ve kompakt kontrol ölçüleri kullanılır.

[Her sayfanın görseli ve davranışı](SAYFALAR.md) - [Yerleşim ve bileşen kuralları](KURALLAR.md) - [Komut envanteri](KOMUTLAR.md) - [Canlı inceleme ve sınırlar](INCELEME.md) - [Paket denetimi](DOGRULAMA.md)

Görsellerdeki veri, ölçüm, yol, revizyon, sürüm ayrıntısı ve kod örnekleri temsili yerleşim içeriğidir. Uygulama davranışı için uygulama kaydı ve mevcut kaynak işlevleri esas alınır. Sayfa 32–36 için gerçek Classic adaptörleri eklenmiştir; kurulum ve çalışma zamanı sınırları araçlar kaydındadır. Help dış bağlantıları sonraki aşamaya bırakılmıştır.

## SQL ve veri

| Sayfa | Görsel | Ana eylem |
| --- | --- | --- |
| 01 | [SQL çalışma alanı](gorseller/01-sql-editoru.png) | Çalıştır |
| 02 | [Veri tablosu ve hücre ayrıntısı](gorseller/02-veri-tablosu.png) | SQL’i incele |
| 03 | [Sorgu planı](gorseller/03-sorgu-plani.png) | Planı al |
| 04 | [Sorgu geçmişi](gorseller/04-sorgu-gecmisi.png) | Editöre aktar |
| 05 | [SQL parçaları](gorseller/05-sql-parcalari.png) | İmlece ekle |
| 06 | [Bağlamsal SQL yardımı](gorseller/06-sql-yardimi.png) | İmleçteki komutu göster |
| 11 | [Tablo verisinde arama](gorseller/11-veride-arama.png) | Ara |
| 28 | [CSV ve JSON aktarımı](gorseller/28-csv-json-aktarimi.png) | İçe aktarmayı başlat |
| 30 | [SQL betik çalıştırma](gorseller/30-sql-betik-calistirma.png) | Betiği çalıştır |
| 40 | [Hücre ve BLOB inceleyicisi](gorseller/40-hucre-blob.png) | Kapat |
| 41 | [CSV ve JSON dışa aktarma](gorseller/41-csv-json-disa-aktarma.png) | Dışa aktarmayı başlat |

## Şema ve nesneler

| Sayfa | Görsel | Ana eylem |
| --- | --- | --- |
| 07 | [Şema ve nesne inceleyicisi](gorseller/07-sema-inceleyicisi.png) | Tanımı düzenle |
| 08 | [Tablo tasarımı](gorseller/08-tablo-tasarimi.png) | SQL’i incele |
| 09 | [Görünüm düzenleyicisi](gorseller/09-gorunum-duzenleyicisi.png) | SQL’i incele |
| 10 | [Yordam ve fonksiyon düzenleyicisi](gorseller/10-rutin-duzenleyicisi.png) | SQL’i incele |
| 26 | [Sunucudan model çıkarma](gorseller/26-model-cikarma.png) | Modeli oluştur |
| 27 | [Şema aktarımı](gorseller/27-sema-aktarimi.png) | Aktarım seçenekleri |
| 29 | [SQL değişiklik incelemesi](gorseller/29-sql-degisiklik-incelemesi.png) | Uygula |
| 37 | [Veri türü eşleştirme](gorseller/37-tur-eslestirme.png) | Kapat |
| 38 | [Tablo bakımı](gorseller/38-tablo-bakimi.png) | SQL’i incele |
| 39 | [Mekânsal veri aktarımı](gorseller/39-mekansal-veri.png) | İleri |

## Sunucu yönetimi ve performans

| Sayfa | Görsel | Ana eylem |
| --- | --- | --- |
| 12 | [Sunucu özeti](gorseller/12-sunucu-ozeti.png) | Yenile |
| 13 | [İstemci oturumları](gorseller/13-istemci-oturumlari.png) | Seçilen oturumu incele |
| 14 | [Kullanıcılar ve yetkiler](gorseller/14-kullanicilar-yetkiler.png) | SQL’i incele |
| 15 | [Durum ve sistem değişkenleri](gorseller/15-degiskenler.png) | Gözden geçir |
| 16 | [Performans izleme](gorseller/16-performans-izleme.png) | İzlemeyi sürdür |
| 17 | [Performans raporları](gorseller/17-performans-raporlari.png) | SQL’i editörde aç |
| 18 | [Performans ölçüm ayarları](gorseller/18-olcum-ayarlari.png) | Değişiklikleri incele |
| 19 | [SQL dışa aktarma](gorseller/19-sql-disa-aktarma.png) | Dışa aktarmayı başlat |
| 20 | [SQL içe aktarma](gorseller/20-sql-ice-aktarma.png) | İçe aktarmayı başlat |
| 21 | [Sunucu hizmeti](gorseller/21-sunucu-hizmeti.png) | Profili yapılandır |
| 22 | [Sunucu günlükleri](gorseller/22-sunucu-gunlukleri.png) | Yenile |
| 23 | [Seçenek dosyası](gorseller/23-yapilandirma.png) | Değişiklikleri incele |

## Uygulama araçları

| Sayfa | Görsel | Ana eylem |
| --- | --- | --- |
| 24 | [Uygulama tercihleri](gorseller/24-tercihler.png) | Uygula |
| 25 | [Betikler ve eklentiler](gorseller/25-betikler.png) | Çalıştır |
| 31 | [Yardım ve ürün bilgisi](gorseller/31-yardim-hakkinda.png) | Sürüm bilgisini kopyala |

## StarDataParadise araçlarının tasarım referansları

| Sayfa | Görsel | Ana eylem |
| --- | --- | --- |
| 32 | [Veri değişiklik geçmişi](gorseller/32-veri-gecmisi.png) | Geri yükleme önizlemesi |
| 33 | [Decision kuralları](gorseller/33-decision-kurallari.png) | SQL üret |
| 34 | [CPU model kayıtları](gorseller/34-cpu-modelleri.png) | Seçili sürümü incele |
| 35 | [Compute işleri ve zamanlamalar](gorseller/35-compute-isleri.png) | İş gönder |
| 36 | [Doğrulanmış yedekler](gorseller/36-yedekler.png) | Yeniden doğrula |
