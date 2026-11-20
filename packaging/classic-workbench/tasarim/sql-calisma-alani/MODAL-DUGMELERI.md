# Ortak modal düğmeleri - ilk uygulama

20 Kasım 2026. Kullanıcı önce dosya adı ve açılış düzeltmelerinin sürüm artırmadan commit/push edilmesini istedi. Workbench `a98eb36` ve Server kabul kaydı `b309a79` master'a gönderildikten sonra yeni çalışma `codex/ai-sql-hist-release` dalında başladı.

[Tasarım kuralı](KURALLAR.md#modal-düğmeleri) normal modal işlem düğmelerinin koyu petrol yeşili `#043133`, yazılarının beyaz olmasını ister. `SdpModalStyle` bu stili uygulamanın WinForms modal pencerelerine ilk çizimden önce ve mforms form/wizard girişlerine gösterim öncesinde uygular. İç içe ve sonradan eklenen denetimler aynı yolu kullanır. Disabled/reenabled durumları güncellenir; metin, callback, DialogResult, tab sırası, AcceptButton ve CancelButton değiştirilmez. Yüksek kontrast ve açık native sistem çizimi korunur.

Bu ilk dilim uygulamanın kendi WinForms/mforms düğmelerini kapsar. Windows'un ayrı native TaskDialog, dosya seçici ve güvenlik pencereleri işletim sisteminin çizimini kullanır. AI/Knowledge/HIST ekranlarının tamamı, native sunucu komutları ve SDK sözleşmeleri henüz uygulanmış sayılmaz. Mevcut History uyumluluk katmanının satır olayları, transaction çapında HIST revision/branch/merge desteği değildir. Bu çalışma yeni stable release veya installer teslimi değildir.

## Doğrulama

| Sonuç | Kontrol |
| --- | --- |
| PASS | WBControls, mforms.wr ve StarDataParadise hedefleri ayrı `out/classic-ai-hist-ui` dizinine derlendi. `out/classic-modal-acceptance/build.log`, çıkış 0. |
| PASS | Native sistem çizimini koruyan son güncelleme WBControls hedefinde yeniden derlendi. `build-final.log`, çıkış 0. |
| PASS | Gerçek paketlenmiş WBControls ile 10 modal kontrolü: normal/iptal renkleri, default/cancel kimlikleri ve tab sırası, disabled/reenabled, dinamik düğme, native güvenlik çizimi, gerçek odak, piksel dolgusu ve modal dönüş sonucu. `probe-runtime.log`, çıkış 0. |
| PASS | Mevcut 13 açılış kontrolü yeni çalışma zamanında hem Türkçe hem İngilizce geçti. |
| PASS | Yeni çalışma zamanındaki 126 PE, 1.318 native import, 19 CLR assembly ve 146 başvuru denetimi geçti; eski ürün DLL adı veya bozuk başvuru bulunmadı. |
| FAIL / giderildi | İlk C# çağrısında göreli slash yolları derleyiciye yanlış çözüldü; mutlak Windows yolları kullanıldı. |
| FAIL / giderildi | İlk modal testi EnterThreadModal olayının form kaydından önce gelebileceğini gösterdi. Ortak stil ilk queued paint öncesine taşındı; sonraki kaynak ve gerçek paket testleri geçti. Test callback düzenlemesindeki bir kapanış ayracı derleme hatası da giderildi. |
| NOT_RUN | Gerçek yüksek kontrast/DPI/ekran okuyucu matrisi, MSI kur/kaldır, yeni AI SQL/HIST sunucu kabulü ve SDK aile kabulü. |

```powershell
& packaging/classic-workbench/tests/Test-ModalStyle.ps1 -Runtime out/classic-ai-hist-ui
& packaging/classic-workbench/tests/Test-StartupUi.ps1 -Runtime out/classic-ai-hist-ui -Language tr
& packaging/classic-workbench/tests/Test-StartupUi.ps1 -Runtime out/classic-ai-hist-ui -Language en
python -B packaging/classic-workbench/tests/artifact_runtime_acceptance.py --runtime out/classic-ai-hist-ui
```

Sürüm alanları ve etiketler bu başlangıç diliminde değiştirilmedi. Release ve kurulum sihirbazı adımı, yeni özelliklerin gerçek kabulünden sonra gelir.
