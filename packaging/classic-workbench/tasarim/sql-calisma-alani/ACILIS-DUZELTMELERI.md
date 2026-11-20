# İlk sekme ve SQL editörü açılışı

20 Kasım 2026. Kullanıcının iki görsel üzerinden bildirdiği ilk SQL sekmesindeki eksik yüzey boyaması ve eski “Opening SQL Editor” bekleme kutusu düzeltildi.

İlk sekmenin ölçüsü 19 px yüksekliğinde önbelleğe alınabiliyordu. Form durumu yüklendiğinde yükseklik 32 DIP'e çıkarılıyor, ilk sekmenin çizim alanı eski ölçüde kalıyordu. `FlatTabControl.ItemSize` artık çizim ve isabet alanlarını yeniden hesaplatır. SQL sekmesinin nihai ölçüsü ilk belge eklenmeden atanır. Seçili sekme beyaz, diğer sekmeler `#E7EEEF` yüzeyini tam yükseklikte kullanır.

SQL bağlantısının bekleme sunumu ana pencerenin altındaki 36 DIP yüksekliğinde düz satırdır: bağlantı adı, hareketli ilerleme göstergesi ve **Vazgeç**. Ayrı pencere, logo ve çerçeveli kutu yoktur. Ana sayfa dışından, komut satırından veya oturum geri yüklemesinden başlayan bağlantılar da bu yolu kullanır. Ana sayfadaki seçili profilin mevcut satır içi durumu korunur.

Escape, Enter, Vazgeç ve bağlantı sırasında pencereyi kapatma aynı iptal isteğini iletir. Tekrarlanan istek worker'a yeniden gönderilmez. “Vazgeçiliyor…” worker tamamlanana kadar görünür; alttaki bağlantı çağrısının hemen kesildiği varsayılmaz. İşlem bitince satır kaldırılır ve önceki komut etkinlikleri geri yüklenir. Türkçe/İngilizce etiketler dil paketine bağlıdır.

| Gerçek native görüntü | Kanıt |
| --- | --- |
| [Soğuk açılışta ilk sekme](uygulama/acilis/01-ilk-sekme.png) | Çevrimdışı editör, tam yükseklikte beyaz ilk sekme; sorgu çalıştırılmadı. |
| [SQL editörü açılıyor](uygulama/acilis/02-baglanti-bekleme.png) | Ayrı profil ve geçici loopback uç noktasıyla gerçek `--query` açılışı; modal HUD yok. |
| [Vazgeçiliyor](uygulama/acilis/03-vazgeciliyor.png) | Escape sonrası bekleyen iptal ve devre dışı Vazgeç düğmesi. |

## Derleme ve kabul

Başlangıçta Workbench ve Server çalışma ağaçları temizdi. Önceki kaynak tabanı ve açık kullanıcı uygulaması korunur. Yeni native çıktı `out/classic-startup-ui/StarDataParadise.exe`; test profilleri ve ham kanıtlar `out/classic-startup-acceptance/` altındadır. Kanonik kaynakların native derleme ağacındaki altı karşılığı SHA-256 ile eşleşti.

```powershell
& packaging/classic-workbench/Build-Workspace.ps1 -Output out/classic-startup-ui -RuntimeSeed out/classic-data-schema-delivery -Projects @('frontend/windows/WBControls/WBControls.csproj', 'library/forms/winforms/mforms.wr.vcxproj', 'frontend/windows/MySQLWorkbench/MySQLWorkbench.csproj')
& packaging/classic-workbench/tests/Test-StartupUi.ps1 -Runtime out/classic-startup-ui -Language tr
& packaging/classic-workbench/tests/Test-StartupUi.ps1 -Runtime out/classic-startup-ui -Language en
```

| Durum | Gerçek sonuç |
| --- | --- |
| PASS | Başlangıç ve son mevcut Python regresyonları: 66 sınama, yedi grup, dönüş kodu 0. `out/classic-workspace-audit/data-schema-behavior-tests.json`. |
| PASS | Gerçek C# sekme kaydırma metotları: 11 kontrol, dönüş kodu 0. `out/classic-workspace-audit/tab-scroll-layout-validation.json`. |
| FAIL / beklenen başlangıç | Önceki teslim DLL'i ilk sekmenin 19→32 yükseklik değişimini yenilemiyordu. Yeni regresyonun geometri kontrolü eski ikilide başarısız oldu, dönüş kodu 1. `baseline-probe.log`. |
| PASS | WBControls, mforms.wr ve MySQLWorkbench hedeflerinin üçü derlendi. `build-retry.log` ve `out/classic-workspace-audit/workspace-*.log`. Betik tüm hedefleri PASS bildirdikten sonra son kaynak kopyalamasının zararsız Robocopy kodu 2 ile döndü; hedef derleme hatası değildir. |
| PASS | Paketlenmiş gerçek denetimlerde 13 C# kontrolü hem Türkçe hem İngilizce geçti, iki çalıştırma da dönüş kodu 0. `probe-tr.log`, `probe-en.log`. İlk sekmenin önbelleği ve gerçek `DrawControl` pikseli, iki sekmenin boyutu, satırın ana pencerede kalması, dil, tek iptal, komut durumlarının geri yüklenmesi ve sonraki bağlantıda sıfırlama sınandı. |
| PASS | Yeni uygulama ayrı profille gerçek `--query` yolunda yerel geciktirilmiş TCP fixture'ına bağlandı. Ekran ve erişilebilirlik ağacında satır/profil/ilerleme/Vazgeç görüldü. Escape sonrası iptal metni görüldü; worker sonlandığında native günlük `cancelled by user: canceled` kaydetti. SQL handshake'i ve veri yazması yapılmadı. `delayed-profile/log/wb.log`. |
| PASS | Ayrı çevrimdışı uygulamanın soğuk açılışında ilk sekme tam boyandı. Ctrl+T ve Ctrl+Tab ile ikinci sekme/ilk sekmeye dönüş doğrulandı. Test uygulaması normal kapandı, mevcut kullanıcı uygulaması açık kaldı. |
| FAIL / ortam, giderildi | İlk derleme sandbox içinde geçici `subst` sürücü takma adı oluşturamadı. İzinli tekrar aynı betikle üç hedefi tamamladı. `build.log`, `build-retry.log`. |
| FAIL / test hazırlığı, giderildi | İlk C# derleyici çağrısındaki yol ayraçları mutlak Windows yollarıyla düzeltildi. İlk piksel deneyi native `DrawToBitmap` çocuk yüzeyini ölçtüğü için başarısızdı; test üretimin kullandığı gerçek `DrawControl` metodunu çalıştıracak şekilde düzeltildi. Son iki dil kabulü geçti. |
| PASS | Kaynak/native SHA-256 eşliği, dört çalışma zamanı çıktısı, görsel kanıtlar ve whitespace kontrolü. `startup-ui-validation.json`. |
| NOT_RUN | Tam DPI/yüksek kontrast/ekran okuyucu matrisi, canlı SSH ve MSI kur/kaldır bu düzeltmede çalıştırılmadı. |

Sunucu, CLI, DLL sözleşmesi ve sürüm numarası değiştirilmedi. Commit/push veya paket yayını yapılmadı. Ortak kayıt `StarDataParadiseServer/docs/coordination/MYSQL_MIGRATION.md` içindedir.
