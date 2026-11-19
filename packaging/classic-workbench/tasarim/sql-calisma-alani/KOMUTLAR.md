# Komut envanteri

19 Kasım 2026 tarihli bağlı SQL oturumunun canlı menüsü ve Classic kaynakları birlikte incelendi. Menü kapsayıcıları ve devre dışı komutlar da kayıttadır. Dosya seçicisi, metin düzenlemesi ve panel açma gibi işlemler mevcut sayfa üzerinde çalışır.

[Sayfa galerisi](README.md) · [Ham komut kayıtları](komut-envanteri.csv) · [Python kontrol etiketleri ve callback’ler](sayfa-kontrolleri.csv) · [Canlı kontroller](canli-kontroller.csv)

Help dış bağlantılarının yeni hedefleri kullanıcının son isteğiyle sonraki aşamaya bırakılmıştır.

## Menü

| Mevcut giriş | Amaç | Tasarımda erişim | Kaynak komutu |
| --- | --- | --- | --- |
| File | İlgili komut grubunu açar. | Menü grubu | `` |
| File / New Document | Yeni yerel EER modeli oluşturur. | Önceden onaylı Modeller | `plugin:wb.file.newDocument` |
| File / New Query Tab | Etkin bağlantıda yeni SQL tamponu açar. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `builtin:query.newFile` |
| File / Open Model | .mwb model dosyasını açar. | Önceden onaylı Modeller | `plugin:wb.file.openModel` |
| File / Open SQL Script | SQL dosyasını sorgu tamponuna açar. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `builtin:query.openFile` |
| File / Open Recent | Son kullanılan dosyaları listeler. | Dosya türüne göre | `` |
| File / Open Recent / Open Recent File 1 | Son kullanılan dosyayı türüne göre açar. | Dosya türüne göre | `` |
| File / Open Recent / Open Recent File 2 | Son kullanılan dosyayı türüne göre açar. | Dosya türüne göre | `` |
| File / Run SQL Script | SQL dosyasını ayrı runner akışına hazırlar. | [30 · SQL betik çalıştırma](SAYFALAR.md#sayfa-30) | `plugin:wb.sqlide.runScript` |
| File / Close Connection Tab | Bağlantı oturumunu kapatma akışını açar. | Onay/kaydetme iletişimi | `builtin:close_tab` |
| File / Close Tab | Etkin belgeyi, kaydetme durumunu denetleyerek kapatır. | Etkin belge | `builtin:close_editor` |
| File / Save Script | SQL tamponunu dosyaya kaydeder. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `builtin:query.saveFile` |
| File / Save Script As | SQL tamponunu yeni dosya yoluna kaydeder. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `builtin:query.saveFileAs` |
| File / Revert to Saved | Tamponu dosyadaki son kayıtlı içeriğe döndürür. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `builtin:query.revert` |
| File / Exit Application | Kaydetme ve bağlantı kapatma akışıyla uygulamadan çıkar. | Uygulama | `plugin:wb.file.exit` |
| Edit | İlgili komut grubunu açar. | Menü grubu | `` |
| Edit / Undo | Son tampon düzenlemesini geri alır. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `builtin:undo` |
| Edit / Redo | Tampon düzenlemesini tekrarlar. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `builtin:redo` |
| Edit / Cut | Seçimi keser. | Etkin düzenleyici | `builtin:cut` |
| Edit / Copy | Seçimi panoya kopyalar. | Etkin düzenleyici | `builtin:copy` |
| Edit / Paste | Panoyu etkin düzenleyiciye ekler. | Etkin düzenleyici | `builtin:paste` |
| Edit / Delete | Seçili içeriği siler. | Etkin düzenleyici | `builtin:delete` |
| Edit / Select All | Etkin tamponun içeriğini seçer. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `builtin:selectAll` |
| Edit / Select Next Placeholder | SQL şablonundaki sonraki yer tutucuya geçer. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `builtin:query.jump_to_placeholder` |
| Edit / Find | Metin arama alanını açar. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `` |
| Edit / Find / Find | Metin arama alanını açar. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `builtin:find` |
| Edit / Find / Find And Replace | Bul/değiştir alanını açar. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `builtin:find_replace` |
| Edit / Format | İlgili komut grubunu açar. | Menü grubu | `` |
| Edit / Format / Beautify Query | SQL tamponunu biçimlendirir. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `plugin:wb.sqlide.enbeautificate` |
| Edit / Format / Upcase Keywords | SQL anahtar sözcüklerini büyük harfe dönüştürür. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `plugin:wb.sqlide.upcaseKeywords` |
| Edit / Format / Lowcase Keywords | SQL anahtar sözcüklerini küçük harfe dönüştürür. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `plugin:wb.sqlide.lowercaseKeywords` |
| Edit / Format / Toggle Comment on Selection | Seçili SQL yorumunu açar/kapatır. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `plugin:wb.sqlide.comment` |
| Edit / Auto Complete | Nesne ve sözcük tamamlama listesini açar. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `builtin:list-members` |
| Edit / Preferences | Uygulama tercihlerini açar. | [24 · Uygulama tercihleri](SAYFALAR.md#sayfa-24) | `plugin:wb.form.showOptions` |
| View | İlgili komut grubunu açar. | Menü grubu | `` |
| View / Home | Ana sayfaya geçer. | Önceden onaylı ana sayfa | `builtin:overview.home` |
| View / Panels | İlgili komut grubunu açar. | Menü grubu | `` |
| View / Panels / Toggle Sidebar | Sol nesne/yönetim panelini açar veya daraltır. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `builtin:alias.wb.toggleSidebar` |
| View / Panels / Toggle Secondary Sidebar | Yardım/parçalar çekmecesini açar veya kapatır. | [05 · SQL parçaları](SAYFALAR.md#sayfa-05), [06 · Bağlamsal SQL yardımı](SAYFALAR.md#sayfa-06) | `builtin:alias.wb.toggleSecondarySidebar` |
| View / Panels / Toggle Output Area | Mesaj ve geçmiş alanını açar veya daraltır. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01), [04 · Sorgu geçmişi](SAYFALAR.md#sayfa-04) | `builtin:alias.wb.toggleOutputArea` |
| View / Select Next Main Tab | Sonraki ana bağlantı/belge sekmesine geçer. | Onaylı sekme şeridi | `builtin:wb.next_tab` |
| View / Select Previous Main Tab | Önceki ana bağlantı/belge sekmesine geçer. | Onaylı sekme şeridi | `builtin:wb.back_tab` |
| Query | İlgili komut grubunu açar. | Menü grubu | `` |
| Query / Execute All or Selection | Seçimi, seçim yoksa tamponun tamamını sunucuya gönderir. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `builtin:query.execute` |
| Query / Execute to Text | SQL’i çalıştırır ve metin çıktısına yönlendirir. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `plugin:wb.sqlide.executeToTextOutput` |
| Query / Execute Current Statement | İmleçteki tek SQL ifadesini çalıştırır. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `builtin:query.execute_current_statement` |
| Query / Execute Current Statement to Text Output | İmleçteki ifadenin çıktısını metin olarak gösterir. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `plugin:wb.sqlide.verticalOutput` |
| Query / Explain Current Statement | İmleçteki ifade için plan alır. | [03 · Sorgu planı](SAYFALAR.md#sayfa-03) | `builtin:query.explain_current_statement` |
| Query / Stop | Çalışan sorguyu iptal eder. Açık işlem bağlantıda kalır. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `builtin:query.cancel` |
| Query / Stop Script Execution on Errors | Hata sonrası betik yürütme davranışını değiştirir. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `builtin:query.continueOnError` |
| Query / Limit Rows | SELECT için otomatik satır sınırını seçer. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `` |
| Query / Limit Rows / Do Not Limit | SELECT sonuçları için seçilen satır sınırını uygular. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `query.limit_rows (dinamik seçenek)` |
| Query / Limit Rows / Limit to 10 Rows | SELECT sonuçları için seçilen satır sınırını uygular. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `query.limit_rows (dinamik seçenek)` |
| Query / Limit Rows / Limit to 50 Rows | SELECT sonuçları için seçilen satır sınırını uygular. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `query.limit_rows (dinamik seçenek)` |
| Query / Limit Rows / Limit to 100 Rows | SELECT sonuçları için seçilen satır sınırını uygular. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `query.limit_rows (dinamik seçenek)` |
| Query / Limit Rows / Limit to 200 Rows | SELECT sonuçları için seçilen satır sınırını uygular. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `query.limit_rows (dinamik seçenek)` |
| Query / Limit Rows / Limit to 300 Rows | SELECT sonuçları için seçilen satır sınırını uygular. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `query.limit_rows (dinamik seçenek)` |
| Query / Limit Rows / Limit to 400 Rows | SELECT sonuçları için seçilen satır sınırını uygular. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `query.limit_rows (dinamik seçenek)` |
| Query / Limit Rows / Limit to 500 Rows | SELECT sonuçları için seçilen satır sınırını uygular. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `query.limit_rows (dinamik seçenek)` |
| Query / Limit Rows / Limit to 1000 Rows | SELECT sonuçları için seçilen satır sınırını uygular. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `query.limit_rows (dinamik seçenek)` |
| Query / Limit Rows / Limit to 2000 Rows | SELECT sonuçları için seçilen satır sınırını uygular. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `query.limit_rows (dinamik seçenek)` |
| Query / Limit Rows / Limit to 5000 Rows | SELECT sonuçları için seçilen satır sınırını uygular. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `query.limit_rows (dinamik seçenek)` |
| Query / Limit Rows / Limit to 10000 Rows | SELECT sonuçları için seçilen satır sınırını uygular. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `query.limit_rows (dinamik seçenek)` |
| Query / Limit Rows / Limit to 50000 Rows | SELECT sonuçları için seçilen satır sınırını uygular. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `query.limit_rows (dinamik seçenek)` |
| Query / Collect Performance Schema Statistics | Sorgu için ek Performance Schema istatistiği toplamayı seçer. | [03 · Sorgu planı](SAYFALAR.md#sayfa-03) | `builtin:query.gatherPSInfo` |
| Query / Reconnect to Server | Etkin profille yeniden bağlantı kurar. | Bağlantı bekleme taslağı1 | `builtin:query.reconnect` |
| Query / New Tab to Current Server | Aynı sunucuya bağımsız bağlantı oturumu açar. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `builtin:query.new_connection` |
| Query / Auto Commit Transactions | Bağlantı düzeyinde autocommit kipini değiştirir. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `builtin:query.autocommit` |
| Query / Commit Transaction | Bağlantının açık işlemini commit eder. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `builtin:query.commit` |
| Query / Rollback Transaction | Bağlantının açık işlemini rollback eder. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `builtin:query.rollback` |
| Query / Commit Result Edits | Sonuç tablosunun bekleyen satır değişikliklerini uygular. | [02 · Veri tablosu ve hücre ayrıntısı](SAYFALAR.md#sayfa-02), [29 · SQL değişiklik incelemesi](SAYFALAR.md#sayfa-29) | `builtin:query.save_edits` |
| Query / Discard Result Edits | Sonuç tablosunun bekleyen değişikliklerini geri alır. | [02 · Veri tablosu ve hücre ayrıntısı](SAYFALAR.md#sayfa-02) | `builtin:query.discard_edits` |
| Query / Export Results | Sonuç kümesini dosyaya yazma seçicisini açar. | [41 · CSV ve JSON dışa aktarma](SAYFALAR.md#sayfa-41) | `builtin:query.export` |
| Database | İlgili komut grubunu açar. | Menü grubu | `` |
| Database / Connect to Database | Bağlantı seçme/yeni bağlantı formunu açar. | Önceden onaylı kompakt bağlantı | `plugin:wb.form.showQueryConnectDialog` |
| Database / Manage Connections | Kayıtlı profilleri yönetme penceresini açar. | Önceden onaylı Profilleri yönet | `plugin:wb.form.showInstanceManager` |
| Database / Reverse Engineer | Sunucudan yerel EER modeline metadata aktarır. | [26 · Sunucudan model çıkarma](SAYFALAR.md#sayfa-26) | `plugin:wb.file.newDocumentFromDB` |
| Database / Schema Transfer Wizard | Uyumlu sunucular arasında şema kopyalama akışını açar. | [27 · Şema aktarımı](SAYFALAR.md#sayfa-27) | `plugin:wb.db.copy.open` |
| Database / Migration Wizard | Kaynak sağlayıcıdan hedefe Veri Taşıma akışını açar. | Önceden onaylı Veri Taşıma | `plugin:wb.migration.open` |
| Database / Edit Type Mappings for Generic Migration | Genel veri türü eşleştirme kurallarını düzenler. | [37 · Veri türü eşleştirme](SAYFALAR.md#sayfa-37) | `plugin:wb.migration.showTypeMapEditor` |
| Database / Search Table Data | Seçili tablolarda veri aramasını açar. | [11 · Tablo verisinde arama](SAYFALAR.md#sayfa-11) | `plugin:com.mysql.wb.menu.database.search` |
| Server | İlgili komut grubunu açar. | Menü grubu | `` |
| Server / Server Status | Sunucu durumunu ve özelliklerini gösterir. | [12 · Sunucu özeti](SAYFALAR.md#sayfa-12) | `plugin:wb.admin.open_into:admin_server_status` |
| Server / Client Connections | Sunucu oturumlarını ve çalışan sorguları gösterir. | [13 · İstemci oturumları](SAYFALAR.md#sayfa-13) | `plugin:wb.admin.open_into:admin_connections` |
| Server / Users and Privileges | Hesap ve yetki yönetimini açar. | [14 · Kullanıcılar ve yetkiler](SAYFALAR.md#sayfa-14) | `plugin:wb.admin.open_into:admin_manage_privs` |
| Server / Status and System Variables | Sayaç ve sistem değişkenlerini açar. | [15 · Durum ve sistem değişkenleri](SAYFALAR.md#sayfa-15) | `plugin:wb.admin.open_into:admin_status_vars` |
| Server / Data Export | SQL dökümü dışa aktarmayı açar. | [19 · SQL dışa aktarma](SAYFALAR.md#sayfa-19) | `plugin:wb.admin.open_into:admin_export` |
| Server / Data Import | SQL dökümü yüklemeyi açar. | [20 · SQL içe aktarma](SAYFALAR.md#sayfa-20) | `plugin:wb.admin.open_into:admin_restore_data` |
| Server / Startup or Shutdown | Profilin hizmet yönetimi sayfasını açar. | [21 · Sunucu hizmeti](SAYFALAR.md#sayfa-21) | `plugin:wb.admin.open_into:admin_start_stop` |
| Server / Server Logs | Sunucu günlük okuyucusunu açar. | [22 · Sunucu günlükleri](SAYFALAR.md#sayfa-22) | `plugin:wb.admin.open_into:admin_server_logs` |
| Server / Options File | Sunucu seçenek dosyasını düzenler. | [23 · Seçenek dosyası](SAYFALAR.md#sayfa-23) | `plugin:wb.admin.open_into:admin_option_file` |
| Server / Dashboard | Canlı performans ölçümlerini gösterir. | [16 · Performans izleme](SAYFALAR.md#sayfa-16) | `plugin:wb.admin.open_into:admin_dashboard` |
| Server / Performance Reports | Toplanan performans raporlarını gösterir. | [17 · Performans raporları](SAYFALAR.md#sayfa-17) | `plugin:wb.admin.open_into:admin_performance_reports` |
| Server / Performance Schema Setup | Ölçüm yapılandırmasını açar. | [18 · Performans ölçüm ayarları](SAYFALAR.md#sayfa-18) | `plugin:wb.admin.open_into:admin_instrumentation_setup` |
| Server / Audit Log Inspector | Kurumsal audit eklentisi girişini açar. | Eklentiye bağlı | `plugin:wb.admin.open_into_se:admin_audit_inspector` |
| Server / Firewall | Kurumsal firewall eklentisi girişini açar. | Eklentiye bağlı | `plugin:wb.admin.open_into_se:admin_firewall` |
| Server / Enterprise Backup | Kurumsal yedekleme eklentisi girişidir. | [36 · Doğrulanmış yedekler](SAYFALAR.md#sayfa-36) | `plugin:wb.admin.open_into_se:admin_meb_backup` |
| Server / Backup Recovery | Kurumsal yedek kurtarma eklentisi girişidir. | [36 · Doğrulanmış yedekler](SAYFALAR.md#sayfa-36) | `plugin:wb.admin.open_into_se:admin_meb_restore` |
| Server / Management Access Settings | Profilin sunucu yönetimi ayarlarını açar. | Önceden onaylı Profilleri yönet | `plugin:wb.admin.settings` |
| Server / Reset Saved Passwords for Connection | Bağlantı için saklanan parola kayıtlarını temizleme akışını açar. | Bağlantı profili | `plugin:wb.admin.reset_password_cache` |
| Tools | İlgili komut grubunu açar. | Menü grubu | `` |
| Tools / Browse Audit Log File | Audit dosyası okuyucusunu çağırır. Eklenti desteği gerekir. | Eklentiye bağlı | `call:WbAdmin.openAuditFile` |
| Tools / Configuration | İlgili komut grubunu açar. | Menü grubu | `` |
| Tools / Configuration / Backup Connections | Profil ayarlarını yerel yedek dosyasına aktarır. | Profil dosya seçicisi | `plugin:wb.tools.backupConnections` |
| Tools / Configuration / Restore Connections | Yerel yedekten profil ayarlarını geri alma akışını açar. | Profil dosya seçicisi | `plugin:wb.tools.restoreConnections` |
| Tools / SQL Utilities | İlgili komut grubunu açar. | Menü grubu | `` |
| Tools / SQL Utilities / Copy Server Connection as PHP | Bağlantı için PHP kod örneğini panoya alır. | Araçlar → Kod örnekleri | `` |
| Tools / SQL Utilities / Copy Select Iteration as PHP Code | Sonuç kümesini dolaşan PHP örneğini panoya alır. | Araçlar → Kod örnekleri | `` |
| Tools / MySQL Utilities Shell | Eski harici Utilities aracını arayan girişidir. | Araçlar → Harici araçlar | `` |
| Scripting | İlgili komut grubunu açar. | Menü grubu | `` |
| Scripting / Scripting Shell | Python/GRT betik penceresini açar. | [25 · Betikler ve eklentiler](SAYFALAR.md#sayfa-25) | `plugin:wb.form.showGRTShell` |
| Scripting / New Script | Yeni Python/GRT dosyası oluşturur. | [25 · Betikler ve eklentiler](SAYFALAR.md#sayfa-25) | `plugin:wb.form.newGRTFile` |
| Scripting / Open Script | Python/GRT dosyası açar. | [25 · Betikler ve eklentiler](SAYFALAR.md#sayfa-25) | `plugin:wb.form.openGRTFile` |
| Scripting / Run Script File | Etkin betiği çalıştıran bağlamsal komuttur. | [25 · Betikler ve eklentiler](SAYFALAR.md#sayfa-25) | `` |
| Scripting / Run Workbench Script File | Dosyadan Python/GRT betiği çalıştırma akışını açar. | [25 · Betikler ve eklentiler](SAYFALAR.md#sayfa-25) | `plugin:wb.tools.runScriptFile` |
| Scripting / Install Plugin or Module | Yerel eklenti/modül paketini yükleme akışını açar. | [25 · Betikler ve eklentiler](SAYFALAR.md#sayfa-25) | `plugin:wb.tools.installModuleFile` |
| Scripting / Plugin Manager | Kurulu modülleri ve ayrıntılarını listeler. | [25 · Betikler ve eklentiler](SAYFALAR.md#sayfa-25) | `plugin:wb.form.showPluginManager` |
| Help | İlgili komut grubunu açar. | Menü grubu | `` |
| Help / Help Index | SQL yardım kaynağını açar. | [06 · Bağlamsal SQL yardımı](SAYFALAR.md#sayfa-06), [31 · Yardım ve ürün bilgisi](SAYFALAR.md#sayfa-31) | `builtin:help_index` |
| Help / Show License Info | Bileşen lisanslarını açar. | [31 · Yardım ve ürün bilgisi](SAYFALAR.md#sayfa-31) | `builtin:show-license` |
| Help / StarDataParadise Documentation | Dış hedefin ürün içeriğiyle doldurulması kullanıcı isteğiyle sonraki aşamaya bırakıldı. | Ertelendi | `builtin:web_mysql_docs` |
| Help / StarDataParadise Blog | Dış hedefin ürün içeriğiyle doldurulması kullanıcı isteğiyle sonraki aşamaya bırakıldı. | Ertelendi | `builtin:web_mysql_blog` |
| Help / StarDataParadise Forums | Dış hedefin ürün içeriğiyle doldurulması kullanıcı isteğiyle sonraki aşamaya bırakıldı. | Ertelendi | `builtin:web_mysql_forum` |
| Help / Upstream Website | Dış hedefin ürün içeriğiyle doldurulması kullanıcı isteğiyle sonraki aşamaya bırakıldı. | Ertelendi | `builtin:web_mysql_home` |
| Help / Oracle Support | Dış hedefin ürün içeriğiyle doldurulması kullanıcı isteğiyle sonraki aşamaya bırakıldı. | Ertelendi | `builtin:web_support` |
| Help / Report a Bug | Dış hedefin ürün içeriğiyle doldurulması kullanıcı isteğiyle sonraki aşamaya bırakıldı. | Ertelendi | `plugin:wb.form.reportBug:from_menu` |
| Help / Locate Log Files | Uygulama günlük klasörünü açar. | [31 · Yardım ve ürün bilgisi](SAYFALAR.md#sayfa-31) | `builtin:locate_log_file` |
| Help / Show Log File | Uygulama günlüğünü açar. | [31 · Yardım ve ürün bilgisi](SAYFALAR.md#sayfa-31) | `builtin:show_log_file` |
| Help / Check for Updates | Dış hedefin ürün içeriğiyle doldurulması kullanıcı isteğiyle sonraki aşamaya bırakıldı. | Ertelendi | `plugin:wb.tools.checkForUpdates` |
| Help / About Workbench | Ürün/sürüm bilgisini gösterir. | [31 · Yardım ve ürün bilgisi](SAYFALAR.md#sayfa-31) | `builtin:show_about` |

## Oturum araç çubuğu

| Mevcut giriş | Amaç | Tasarımda erişim | Kaynak komutu |
| --- | --- | --- | --- |
| query.newFile | Etkin bağlantıda SQL tamponu açar. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `builtin:query.newFile` |
| query.openFile | SQL dosyasını açar. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `builtin:query.openFile` |
| query.show_inspector | Seçilen nesnenin inceleyicisini açar. | [07 · Şema ve nesne inceleyicisi](SAYFALAR.md#sayfa-07) | `builtin:query.show_inspector` |
| query.new_schema | Yeni şemanın ad ve karakter kümesi tanımını hazırlar. | [07 · Şema ve nesne inceleyicisi](SAYFALAR.md#sayfa-07), [29 · SQL değişiklik incelemesi](SAYFALAR.md#sayfa-29) | `builtin:query.new_schema` |
| query.new_table | Yeni tablo tanımını açar. | [08 · Tablo tasarımı](SAYFALAR.md#sayfa-08) | `builtin:query.new_table` |
| query.new_view | Yeni görünüm tanımını açar. | [09 · Görünüm düzenleyicisi](SAYFALAR.md#sayfa-09) | `builtin:query.new_view` |
| query.new_routine | Yeni yordam tanımını açar. | [10 · Yordam ve fonksiyon düzenleyicisi](SAYFALAR.md#sayfa-10) | `builtin:query.new_routine` |
| query.new_function | Yeni fonksiyon tanımını açar. | [10 · Yordam ve fonksiyon düzenleyicisi](SAYFALAR.md#sayfa-10) | `builtin:query.new_function` |
| wb.dbsearch | Tablo verisi arama alanını açar. | [11 · Tablo verisinde arama](SAYFALAR.md#sayfa-11) | `plugin:com.mysql.wb.menu.database.search` |
| query.reconnect | Etkin profille yeniden bağlanır. | Bağlantı bekleme taslağı1 | `builtin:query.reconnect` |
| query.preferences | Tercihleri açar. | [24 · Uygulama tercihleri](SAYFALAR.md#sayfa-24) | `call:Workbench.showOptions` |
| wb.toggleSidebar | Nesne gezginini açar/daraltır. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `builtin:wb.toggleSidebar` |
| wb.toggleOutputArea | Mesaj/geçmiş alanını açar/daraltır. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01), [04 · Sorgu geçmişi](SAYFALAR.md#sayfa-04) | `builtin:wb.toggleOutputArea` |
| wb.toggleSecondarySidebar | Yardım/parçalar çekmecesini açar/kapatır. | [05 · SQL parçaları](SAYFALAR.md#sayfa-05), [06 · Bağlamsal SQL yardımı](SAYFALAR.md#sayfa-06) | `builtin:wb.toggleSecondarySidebar` |

## SQL editörü araç çubuğu

| Mevcut giriş | Amaç | Tasarımda erişim | Kaynak komutu |
| --- | --- | --- | --- |
| Open File | SQL dosyasını açar. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `query.openFile` |
| Save File | Tamponu dosyaya kaydeder. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `query.saveFile` |
| Execute | Seçimi, seçim yoksa tamponun tamamını sunucuya gönderir. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `query.execute` |
| Execute Current Statement | İmleçteki tek SQL ifadesini çalıştırır. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `query.execute_current_statement` |
| Explain Current Statement | İmleçteki ifade için plan alır. | [03 · Sorgu planı](SAYFALAR.md#sayfa-03) | `query.explain_current_statement` |
| Cancel | Çalışan sorguyu iptal eder. Açık işlem bağlantıda kalır. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `query.cancel` |
| Continue On Error | Hata sonrası betik yürütme davranışını değiştirir. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `query.continueOnError` |
| Commit | Bağlantının açık işlemini commit eder. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `query.commit` |
| Rollback | Bağlantının açık işlemini rollback eder. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `query.rollback` |
| Auto Commit | Bağlantı düzeyinde autocommit kipini değiştirir. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `query.autocommit` |
| Limit Rows | SELECT için otomatik satır sınırını seçer. | [01 · SQL çalışma alanı](SAYFALAR.md#sayfa-01) | `limit_rows` |
| Add Snippet | Seçili SQL veya ifadeyi parça olarak saklar. | [05 · SQL parçaları](SAYFALAR.md#sayfa-05) | `add_snippet` |

## Nesne ve sonuç bağlam komutları

| Giriş | Amaç | Yüzey |
| --- | --- | --- |
| Varsayılan şema, şemaya filtrele, yenile | Sorgu bağlamı ve nesne listesini değiştirir. | 01,07 |
| Şema/tablo incele | Salt okunur metadata açar. | 07 |
| Select Rows | Seçili tablo için sorgu tamponu ve sonuç açar. | 01,02 |
| Ad / tam ad / CREATE / SELECT / INSERT / UPDATE / DELETE kopyalama | Nesne veya SQL metnini panoya ya da editöre üretir. | 01 |
| Join Selected Tables / Select Row References / Delete With References | Seçilen ilişkilerden SQL metni üretir. Kopyala ve editöre gönder bağlamları korunur. | 01 |
| Create / Alter Schema | Ad, charset ve collation formunu açar. Uygulama SQL incelemesine gider. | 07,29 |
| Create / Alter Table, Create Table Like, şablon düzenle | Tablo tanımı veya yerel şablonu açar. | 08 |
| Drop / Truncate | Nesne ve etkiyi gösteren onay/SQL incelemesi açar. | 29 |
| Tablo bakımı | ANALYZE, CHECK, OPTIMIZE ve CHECKSUM seçeneklerini açar. | 38 |
| Veri içe/dışa aktar | CSV/JSON aktarım sihirbazını açar. | 28,41 |
| Mekânsal veri yükle | SHP ve GDAL aktarımını açar. | 39 |
| Sonuç filtrele, sırayı sıfırla, yenile, satır ekle/sil | Sonuç kümesi ve düzenleme taslağı üzerinde çalışır. | 02 |
| Hücre düzenle/aç, NULL ata, dosyadan yükle/kaydet | Tipli hücre değerini görüntüler veya taslağa taşır. | 02,40 |
| Sonuç/Form Editor/Field Types/Query Stats/Execution Plan | Aynı sonucun tablo, form, tür, ölçüm ve plan görünümleridir. | 02,03,40 |
| Geçmişten kopyala/editöre ekle | Yerel kayıttaki SQL’i yeniden kullanıma hazırlar. | 04 |
| Yardım geri/ileri, otomatik/manual, konuya atla | Bağlamsal SQL başvurusunu yönetir. | 06 |
| Parça kategorisi, ekle/değiştir/düzenle/sil | SQL parça kütüphanesini yönetir. | 05 |

Bağlam menülerinin canlı metin kanıtları [şema](referanslar/mevcut/15-schema-context.txt) ve [tablo](referanslar/mevcut/17-table-context.txt) kayıtlarında bulunur. Python envanterindeki etiketler arasında açıklama etiketleri de vardır. Bu kayıtların sayısı benzersiz düğme sayısını temsil etmez.
