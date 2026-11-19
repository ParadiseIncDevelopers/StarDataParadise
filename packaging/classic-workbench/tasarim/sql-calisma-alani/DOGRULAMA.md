# Tasarım paketi denetimi

19 Kasım 2026. [Galeri](README.md) · [Canlı inceleme](INCELEME.md) · [Kurallar](KURALLAR.md).

## Tamamlanan kontroller

| Kontrol | Sonuç |
| --- | --- |
| Sayfa kimlikleri ve galeri | 01–41 arası her sayfa bir kez listelendi. Her birinin ayrı görseli ve davranış bölümü var. |
| PNG dosyaları | 41 dosyanın imzası doğrulandı. SHA-256 değerleri birbirinden farklı. |
| Görüntü açma | Windows System.Drawing toplam 68 görüntüyü açtı. 41 sayfa görseli, 24 canlı JPEG ve 3 referans mevcut. |
| Kaynak başvuruları | 41 sayfanın kaynak dosyası yolu mevcut. Native kaynak ve kardeş Server/Cli depoları birlikte kullanıldı. |
| Komut kaydı | 130 menü kaydı, 14 oturum araç çubuğu ve 12 editör araç çubuğu tanımı eşleştirildi. 156 kaydın amacı ve hedef yüzeyi dolu. |
| Kontrol adayları | 31 Python modülünden 220 etiket/callback adayı, canlı ağaçlardan 110 kontrol adayı kayıtlı. Açıklama etiketleri ve tekrar eden yüzeyler bu sayılara dahildir. |
| Markdown başvuruları | Paket içindeki yerel dosya ve sayfa çapası bağlantıları çözümlendi. |
| Görsel inceleme | Sayfalar üretimden sonra gözle incelendi. Seçili alanlar, SQL örnekleri, bağımsız sütun bayrakları ve ENABLED/TIMED anlamı için düzeltmeler yapıldı. |
| Kullanıcı seçimi | Sekmeler 3, bağlantı beklemesi 1 referansları kaydedildi. Help dış bağlantıları ertelendi. |
| Üretim yaması | Mevcut Classic patch SHA-256 değeri `845cb26ccc8e11f3206f13ab0d7b77d5338c29c76e22ec7f958f51b8b5d6c799` olarak korundu. |

Denetim betikleri ve ham sonuçlar Workbench `out/classic-workspace-audit/verify_package.py`, `package-verification.json` ve `image-decoding.json` dosyalarında bulunur. `out/` ignore edilen yerel çalışma çıktısıdır. Kalıcı okuma paketi bu klasördedir.

## Canlı kabulün kapsamı

Canlı SQL yürütmesi iki sentetik satır üreten sabit SELECT/UNION ve bunun EXPLAIN sonucu ile sınırlıydı. Yönetim, sonuç, nesne ve tercih yüzeyleri açılarak incelendi. Hesap/yetki değişikliği, servis başlatma/durdurma, oturum sonlandırma, tablo değişikliği ve aktarım başlatılmadı. İnceleme için açılan SQL tamponu oturumda kaldı.

Günlük ve seçenek dosyası sayfalarında profilin yapılandırma yolu eksikti. Ölçüm ayarları sayfası boş kaldı, nedeni teşhis edilmedi. Bu yüzeylerin taslak içeriği ilgili kaynak kodundan çıkarıldı. Bu bulgulara ait gerçek görüntüler [inceleme kaydında](INCELEME.md) bulunur.

## Uygulama aşamasına kalanlar

Bu paket görsel ve etkileşim tasarımıdır. Bu etapta native derleme veya yeni UI için çalışır kabul yapılmadı. Görsellerdeki SQL, metrik, sürüm ve dosya yolları örnektir. Kesin ölçüler [kurallar belgesinde](KURALLAR.md) tanımlıdır. Uygulamada veri ve SQL gerçek işleyicilerden alınır.

Veri geçmişi, Decision, CPU modelleri, Compute ve yedekler için beş Classic adaptörü önerildi. Bu ekranların kuruluma bağlı durumu, hata/iptal akışı ve gerçek işlem çıktıları uygulama sırasında bağlanacaktır. Satırda bağlantı beklemesinin native modal/asenkron yaşam döngüsüne uyarlanması da bu pakette planlandı.

Tam DPI, yüksek kontrast, ekran okuyucu ve bütün sayfalarda klavye dolaşımı kabulü uygulama aşamasına aittir. Mevcut sınırlamalar [canlı inceleme kaydında](INCELEME.md) ayrıca belirtilmiştir.
