# Görsel galeri doğrulaması

Bu kayıt 20 Kasım 2026 tarihinde yerel tasarım teslimi için yapılan kontrolleri içerir. Görseller üretim arayüzü veya sunucu kabul testi değildir.

| Durum | Kontrol | Sonuç |
| --- | --- | --- |
| PASS | PowerShell ve `System.Drawing.Image.FromFile` ile seçili PNG dosyalarının tamamını açma ve ölçme | 34/34 dosya açıldı; tamamı 1717×916 piksel. |
| PASS | Python ile README ve HTML görsel referanslarını dosyalarla karşılaştırma | Her belgede 34 benzersiz görsel var; eksik bağlantı yok; iki liste ve seçili dosyalar eşleşiyor. |
| PASS | Node.js `vm.Script` ile galeri JavaScript'ini derleme ve küçük DOM sınama düzeneğinde çalıştırma | AI 12, SQL/PLSQL 10, HIST 12 öğe; çıkış kodu 0. |
| PASS | Görsel kontrol ve belirlenen metinlerin yeniden üretilmesi | YAML yerine SQL tanımı; doğru WHEN/DO açıklaması; atomik merge, çakışmada retry yok ve INSERT/revision örneği düzeltildi. Önceki denemeler saklandı. |
| NOT_RUN | Galerinin farklı tarayıcı/DPI kombinasyonlarında canlı görsel kabulü | HTML ve referans yapısı sınandı; tam tarayıcı ve erişilebilirlik matrisi çalıştırılmadı. |
| NOT_RUN | Workbench, SQL motoru, sunucu ve SDK çalışma zamanı testleri | Bu teslim görsel tasarımdır; uygulama kodu veya motor işlevi değiştirilmedi. |

Kullanılan araçlar: yerleşik ImageGen, PowerShell/System.Drawing, Python ve Node.js. Örnek veri, durum ve hata kodları temsili içeriktir.

## Kullanıcı düzeltmeleri sonrası kontrol

| Durum | Kontrol | Sonuç |
| --- | --- | --- |
| PASS | Markdown, HTML ve yerel bağlantıların Python ile karşılaştırılması | 35 benzersiz seçili görsel; iki galeri listesi eşleşiyor ve bağlantılar mevcut. |
| PASS | PowerShell `System.Drawing` ile seçili PNG'leri açma ve bitmap olarak çözme | 35/35 açıldı ve çözüldü; tamamı 1717×916 piksel. |
| PASS | Node.js `vm.Script` ve küçük DOM sınama düzeneği | AI/Knowledge 13, SQL/PLSQL 10, HIST 12 öğe; çıkış kodu 0. |
| PASS | `git diff --check` ile güncellenen Classic kural belgesi | Çıkış kodu 0; yalnız mevcut LF/CRLF çalışma kopyası uyarısı görüldü. |
| FAIL / alternatifle PASS | Python Pillow ile görsel çözme denemesi | `PIL` ortamda bulunamadı; paket kurulmadı. Aynı 35 görsel yukarıdaki System.Drawing kontrolüyle doğrulandı. |
| NOT_RUN | Canlı Workbench/sunucu ve tarayıcı/DPI kabulü | Teslim görsel tasarımdır; bu testler çalıştırılmadı. |

Ayraç kuralı Classic `KURALLAR.md` ve bu galerinin README dosyasında örneklerle açıklandı. Eski taslaklar korunarak seçilen sürümler galeriye bağlandı.
