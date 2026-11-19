# Classic migration kabul testi

```powershell
python packaging/classic-workbench/tests/migration_acceptance.py --exe "C:/.../StarDataParadise.exe"
```

Varsayılan çalışma kaynak charset ayarını zorlamaz. Açık UTF-8 charset seçimini ayrıca sınamak için `--source-charset utf8mb4 --artifacts out/mysql-migration/home-ui-migration-utf8-acceptance` eklenebilir.

Çalıştırmadan önce diğer otomatik Classic testlerini kapatın. Test, kardeş `StarDataParadiseServer` deposundaki `compatibility.tests.fixture` aracını kullanır. Varsayılan Server build yolları `--mysqld`, `--mysql` ve `--basedir` ile değiştirilebilir. Native paket içinde `wbcopytables.exe` bulunmalıdır. Yerel TCP bağlantılarını engelleyen sandbox altında test çalıştırıcısına bu bağlantılar için izin gerekir.

Test rastgele localhost portunda yeni datadir ve ayrı Classic profil açar. Kaynak hesabın yalnız sentetik kaynak şemasına `SELECT, SHOW VIEW`; hedef hesabın yalnız sentetik hedef şemasına yetkisi vardır. Hesaplar TLS gerektirir. Kullanıcı profilleri, kayıtlı bağlantılar ve mevcut veritabanları kullanılmaz. Native MySQL bağlantı yolu için harici ODBC sürücüsü gerekmez; diğer RDBMS kaynakları bu testin kapsamı dışındadır ve uygun x64 ODBC/DBAPI sürücüsüne ihtiyaç duyabilir.

`migration_gui_probe.py` yan yana kaynak/hedef girişini, ayrı bağlantı ayarı panellerini, şema seçimini, nesne seçimini, tablo/kolon eşleştirme callback'lerini ve ayrı salt okunur SQL önizleme sayfasını çalıştırır. Ardından şema oluşturma, `DataMigrator`/`wbcopytables` aktarımı ve rapor sayfasıyla 15 sayfalık akışı doğrular. İlerleme görevleri aynı production `task.func` fonksiyonlarıyla senkron yürütülür. Fare, klavye ve pencere yerleşimi ayrıca canlı uygulamada kontrol edilir.

Önizleme öncesi/sonrası Server'ın 16 DDL/DML `Com_*` sayacı eşit olmalıdır. Açık uygulama adımından önce hedef şema oluşmamalı ve kaynak satırları değişmemelidir. Sonrasında üç sentetik satır hem native connector hem bağımsız CLI ile karşılaştırılır: tam BIGINT, DECIMAL, UTF-8 VARCHAR/TEXT, NULL, boş BLOB/VARBINARY, NUL/quote/backslash/0xFF ve tüm 256 bayt değerleri.

Upstream `ui_db_ConnectPanel::connection` setter'ının başarılı seçimden sonra koşulsuz `SystemError` atması Classic yamasında düzeltildi. Probe, önceki ikililer için bu tam hata metnini yakalayan ve seçili profilin kimliği ile parametrelerini geri okuyarak doğrulayan tanılama yolunu içerir. Kullanıldığında sonuç JSON'unda `connection_setter_legacy_diagnostic` bulunur.

Sonuç ve sansürlenmiş süreç günlükleri varsayılan olarak `out/mysql-migration/home-ui-migration-acceptance/` altına yazılır. Geçici Server ve profil test sonunda temizlenir. JSON'daki `ok`, `preview_zero_writes` ve `independent_cli_verified` alanları birlikte değerlendirilmelidir.

## EER model kabulü

`model_gui_probe.py`, Classic'in `--run-script=<tam yol>` seçeneğiyle yeni ve ayrı bir `--configdir=<test profili>` altında çalıştırılır. Gerçek GRT model API'leriyle iki tablo, beş alan, iki birincil anahtar ve bir dış anahtar ilişkisi oluşturur; diyagrama yerleştirir ve `.mwb` dosyasını kaydeder. Açık bir model varsa veya çıktı dosyası zaten varsa üzerine yazmadan durur.

Varsayılan çıktı `out/classic-home-acceptance/kitaplik.mwb` ve `model-fixture-result.json` dosyalarıdır. Tekrar çalıştırmak için `SDP_MODEL_PROBE_OUTPUT` ortam değişkeniyle yeni bir test çıktı dizini verilebilir. Testin model renk/font ayarları yalnız oluşturulan belgeye aittir; kullanıcının genel tercihlerini değiştirmez. Kaydedilen dosya **Modeller → Model aç** yolundan yeniden açılarak diyagram ve ilişki görsel olarak denetlenir.
