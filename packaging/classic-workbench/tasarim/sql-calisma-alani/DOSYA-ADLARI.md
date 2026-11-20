# SDP dosya adları ve yükleme bağları

20 Kasım 2026. Kullanıcı, paket içindeki `mysql` adlı dosyaların `sdp` olarak adlandırılmasını ve ilgili başvuruların güncellenmesini istedi. Önceki sekme/açılış düzeltmeleri korunur. Yeni uygulama `out/classic-sdp-names-ui/StarDataParadise.exe` yolundadır.

| Önceki dosya ailesi | Yeni dosya ailesi |
| --- | --- |
| `db.mysql.parser.grt.*`, `db.mysql.query.grt.*`, `db.mysql.sqlparser.grt.*` | `db.sdp.parser.grt.*`, `db.sdp.query.grt.*`, `db.sdp.sqlparser.grt.*` |
| `db.mysql.grt.*`, `db.mysql.wbp.*`, `db.mysql.diff.reporting.wbp.*` | `db.sdp.grt.*`, `db.sdp.wbp.*`, `db.sdp.diff.reporting.wbp.*` |
| `db.mysql.editors.wbp.be/wr/fe.*` | `db.sdp.editors.wbp.be/wr/fe.*` |
| `mysql.canvas.*`, `mysql.forms.*`, `MySQLCsUtilities.*` | `sdp.canvas.*`, `sdp.forms.*`, `sdp.utilities.*` |
| `mysql.exe`, `mysqldump.exe` | `sdp.exe`, `sdpdump.exe` |
| `mysql_system_status*.vbs`, `mysqlwbmeb.py/vbs` | `sdp_system_status*.vbs`, `sdpwbmeb.py/vbs` |
| `db_mysql_*_grt.py`, veri XML'leri, profil/ikon/şablon dosyaları | `db_sdp_*_grt.py` ve karşılık gelen `sdp` adları |
| `mysql.profiles`, `structs.db.mysql.xml/h` | `sdp.profiles`, `structs.db.sdp.xml/h` |
| `MySQLWorkbench.sln`, uygulamanın eski `.csproj` adı | `StarDataParadiseClassic.sln`, `StarDataParadise.csproj` |

Native DLL'ler yeni proje adlarıyla derlendi; `.lib`, `.exp`, `.ilk`, `.pdb` ve yönetilen wrapper çıktıları aynı yeni adları kullanır. Managed assembly kimlikleri, tüketen projelerin başvuruları, native PE importları, eklenti frontend kayıtları, GRT dosya yolları, Python importları, profil/ikon/şablon başvuruları, MSI dosya listesi ve derleme/sahneleme betikleri güncellendi. Yeni ve mevcut istemci komut satırları bağlantı ayrıştırıcısında kabul edilir; kullanıcının özel istemci yolu ayarı korunur.

[Ad eşlemesi](../../artifact-names.json) derleme/sahneleme denetiminin ortak kaynağıdır. Eski adlar seed'den yeni pakete kopyalanmaz. Yeniden derlenmemiş QA çalıştırılabilirleri paket seed'ine alınmaz. Ayrı çıktı kullanıldığı için eski çalışma zamanları, açık kullanıcı uygulaması, kaynak ve ayarlar korunur.

İki üçüncü taraf dosya kendi zorunlu adıyla kalır: `mysqlcppconn-10-vs14.dll` native import sözleşmesidir; `mysql_native_password.dll` sunucunun seçtiği kimlik doğrulama eklentisidir. Bu dosyalar ve üretici lisansları değiştirilmez. Yeniden adlandırılan `sdp.exe`/`sdpdump.exe` yardımcıları da aynı lisanslı üretici ikilileridir. Kayıtlı model/bağlantıların uyumu için GRT türleri, modül API kimlikleri, sürücü kimlikleri ve SQL/protokol değerleri sürdürülür; bunların yüklediği dosya yolları yenidir.

## Çalıştırılan doğrulama

Temel kaynak sürümü `3c3887e06e2fdb7ef19e007c552cea473e708dcc`; önceki UI düzeltmesi çalışma ağacında bulunuyordu. Başlangıç patch'i ve untracked dosyalar `out/classic-sdp-names-acceptance/` altında korundu. Windows x64, Visual Studio/MSBuild 18, v143, .NET Framework 4.5 hedefi ve mevcut SWIG 4.3.1 kullanıldı.

```powershell
& packaging/classic-workbench/Build-Workspace.ps1 -Output out/classic-sdp-names-ui -RuntimeSeed out/classic-startup-ui
python packaging/classic-workbench/tests/artifact_names_acceptance.py
python packaging/classic-workbench/tests/artifact_runtime_acceptance.py --runtime out/classic-sdp-names-ui
& packaging/classic-workbench/tests/Test-StartupUi.ps1 -Runtime out/classic-sdp-names-ui -Language tr
& packaging/classic-workbench/tests/Test-StartupUi.ps1 -Runtime out/classic-sdp-names-ui -Language en
```

| Durum | Sonuç ve ham kanıt |
| --- | --- |
| PASS | Başlangıç ve ad değişiklikleri sonrasında mevcut 66 Python regresyonu geçti. Ortak rapor `out/classic-workspace-audit/data-schema-behavior-tests.json`. |
| PASS | İstemci adları, özel istemci yolu, yeni/eski komut satırı ve eşleme çakışmaları için üç davranış testi geçti, çıkış 0. `client-lookups.log`. |
| FAIL / beklenen başlangıç | Önceki paketin ad denetimi eski ürün dosyalarını yakaladı, çıkış 1. `baseline-name-audit.log`. |
| PASS | 40 uygulama/kütüphane/eklenti hedefi yeni adlarla derlendi. İlk 37 hedef ve son üç hedefin tekrarı tamamlandı, son betik çıkışı 0. `build.log`, `build-retry.log`. |
| FAIL / giderildi | İlk `_mforms` denemesi SWIG'i komut yolunda bulamadı, MSB8066/9009. `_mforms`/`_cairo` proje makroları ve iki native build betiği aracın açık yolunu kullanacak şekilde düzeltildi; `_mforms`, `_cairo`, ana uygulama tekrarı geçti. Üretilen Python wrapper'ları eşleşen native köprüyle sahnelenir. `first-swig-failure.log`, `build-retry.log`. |
| PASS | 126 PE dosyası ve 1.318 normal/gecikmeli native DLL başvurusu denetlendi. Eski ürün dosyası veya eski ürün DLL importu yok. 12 yeni adlı DLL mevcut. `artifact-bindings.json`. |
| PASS | 19 gerçek CLR assembly ve 146 assembly başvurusu denetlendi; eski ürün assembly kimliği/başvurusu yok. `assembly-bindings.log`. |
| PASS | `sdp.exe --version` ve `sdpdump.exe --version` çıkış 0. Üretici ikilileriyle byte/hash eşliği korundu. İki zorunlu vendor DLL'in hash'i önceki paketle eşleşir. |
| PASS | Paketlenmiş gerçek UI denetimlerinin 13 kontrolü Türkçe ve İngilizcede ayrı ayrı geçti, çıkışlar 0. İlk sekme geometrisi/boyaması ve kutusuz açılış/iptal davranışı korunur. `startup-tr.log`, `startup-en.log`. |
| PASS | Ayrı profilli gerçek uygulamada yedi GRT modülünün yolu yeni DLL/Python adlarına çözüldü. 26 profil şablonu bulundu. Önceki `.mwb` fixture açıldı, yeni parser ile bellekte şema/tablo ayrıştırıldı; tablo editörü canlı UI'de açıldı. Bağlantı ve sunucu yazması yok, model kaydedilmedi. `native-result.json`, [native görüntü](uygulama/dosya-adlari/01-sdp-tablo-editoru.png). |
| PASS | QA normal kapandı, kullanıcının açık uygulaması korundu. Eski model fixture'ının ve test kopyasının dosya hash'leri değişmedi. |
| PASS | 62 proje dosyasının Release_OSS başvuruları, 14 değişen Python dosyası ve 32 XML doğrulandı. Ticari validation projesinin mevcut eksik başvurusu Release_OSS dışında koşulludur; bu yapılandırmada NOT_APPLICABLE. `preflight.json`. |
| PASS | Kaynak/native eşliği, çıktı hash'leri, görüntü decode/bağlantı ve Windows CRLF'yi koruyan whitespace kontrolü. `sdp-names-validation.json`. |
| PASS | `Build-Workspace.ps1 -SyncOnly` çalışma zamanı seed'i olmadan kaynak eşlemesini tamamladı, çıkış 0. Native solution derleyicisi aynı korumalı eşlemeyi çağırır. `sync-only.log`. |
| NOT_RUN | Canlı DB/SSH işlemleri, MSI kur/kaldır ve Linux/macOS derlemesi bu dosya adı kabulünde çalıştırılmadı. Başlangıçtaki video adapter/Service Control Manager ve opsiyonel ticari eklenti günlükleri bu kabulün zorunlu modül yüklemelerini engellemedi. |

Raporun ilk görsel doğrulama denemesinde yerel Python'da Pillow bulunmadı. Ek bağımlılık kurulmadan Windows `System.Drawing` ile 1920×1020 PNG tam açıldı; sonuç `image-decoding.json` içindedir.

Ham kanıtların ana dizini `out/classic-sdp-names-acceptance/` olur. Mevcut kullanıcı uygulaması kapatılmadı; yeni paketi görmek için yeni çalıştırılabilir dosya açılır. Commit/push veya sürüm yayını yapılmadı.

## Native paketi sahneleme

`Stage-Native.ps1` eski native build klasörünü değiştirmeden ayrı ve yeni bir `-Output` oluşturur. Varsayılan çıktı `out/classic-native-sdp-ui` olur. `-BuildOutput` verilmezse native kaynağın `bin/x64/Release_OSS` dizini kullanılır. Gerçek yeni derleme ile şu ayrı paket de doğrulandı:

```powershell
& packaging/classic-workbench/Stage-Native.ps1 -BuildOutput out/classic-sdp-names-ui -Output out/classic-sdp-staged-ui
```

`stage.log` paket tamamlığını, eski ad denetimini ve iki GDAL aracının sürüm çağrısını kaydeder. Staged paketin yeni adlı DLL hash'leri doğrulanmış çalışma zamanıyla eşleşir.

## Sürüm artırmadan kaynak yayını için son kontrol

20 Kasım 2026 tarihinde kullanıcı önce dosya adı ve açılış düzeltmelerinin commit/push edilmesini, sürüm numarasının değiştirilmemesini istedi. Bu commit kapsamı bu iki düzeltme ve kabul kanıtlarıdır; AI SQL, Knowledge, HIST tasarımları ve yeni modal stil çalışması ayrı tutulur.

Son kontrolde mevcut 66 Python regresyonu, üç dosya adı davranış testi, 126 PE/1.318 native import ve 19 CLR assembly/146 başvuru denetimi geçti. Paketlenmiş gerçek denetimlerde 13 açılış kontrolü Türkçe ve İngilizcede yeniden geçti. Windows CRLF satır sonları dikkate alınan `git diff --check` sonucu 0 oldu. Sürüm alanları, etiketler ve dağıtım paketi bu işlemde değiştirilmez. MSI yaşam döngüsü ve canlı DB/SSH yeniden çalıştırılmadı.
