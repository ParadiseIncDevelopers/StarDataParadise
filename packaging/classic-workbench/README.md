# Klasik StarDataParadise Workbench

Tam Classic kaynak ağacı [classic-workbench/](../../classic-workbench/) altında master dalında yayımlanır. Bu klasör upstream kaynakları, StarDataParadise değişikliklerini, ikonları ve lisans bildirimlerini birlikte içerir. Yerel geliştirme ağacı `out/mysql-workbench-native-src` master dalında commit edilmiştir. Aşağıdaki yama akışı da aynı 8.0.47 tabanından kaynak hazırlamak için kullanılabilir.

Bağlantılar, Modeller ve Veri Taşıma ekranlarının renk, tipografi, ikon ve kompakt yerleşim ölçüleri [ana sayfa tasarım kurallarında](tasarim/ANA_SAYFA.md) yer alır.

## 8.0.47 kaynak derlemesi · `v0.3.0-preview.4`

`workbench-8.0.47.patch`, Workbench 8.0.47’nin `dc3590dbfbee29c87e201d4ef53607e5deb0e4f9` commit’ine uygulanır. Yama, StarDataParadise metinlerini ve yıldız ikonlarını, klasik SQL editörüyle ilgili marka değişikliklerini ve Windows kaynak derleme düzeltmelerini içerir. SQL editörünün tasarım dosyası 8.0.47 ile aynı. Karşılama ekranındaki tanıtım bandı mevcut tercih üzerinden varsayılan olarak gizli.

PowerShell 7 ile bu deponun kökünden temiz bir çalışma ağacı hazırlayın.

```powershell
git clone --depth 1 --branch 8.0.47 https://github.com/mysql/mysql-workbench.git out/mysql-workbench-native-src
$patch = (Resolve-Path 'packaging/classic-workbench/workbench-8.0.47.patch').Path
git -C out/mysql-workbench-native-src apply --check $patch
git -C out/mysql-workbench-native-src apply $patch
```

Windows derlemesi için Visual Studio 18, MSVC v143 x64, C++/CLI, ATL ve Windows SDK kullanıldı. Derleme dizinlerine şu bağımlılıkları hazırlayın.

- `out/workbench8-tools/framework` altında .NET Framework 4.5 ve 4.5.2 başvuru derlemeleri. `out/workbench8-tools/swigwin-4.3.1` altında SWIG. `Build-Native.ps1 -JavaHome` ile JDK 17 dizini seçilebilir.
- `out/workbench8-deps/include` ve `lib` altında Boost, GLib, Cairo, libxml2, libzip, zlib, SQLite, VSQLite++, GDAL, Connector/C++, OpenSSL, libssh, ATL ve ANTLR C++ başvuru kitaplıkları ile x64 DLL’ler. `bin` altında ANTLR 4.13.2 JAR, `mysql.exe`, `mysqldump.exe`, `ogrinfo.exe` ve `ogr2ogr.exe`. Çalışma zamanı ayrıca `Python` altında Python 3.12 SDK ve standart kitaplıkları, `share` altında GDAL ve PROJ verilerini, `licenses` altında bağımlılık bildirimlerini kullanır.

```powershell
pwsh -NoProfile -File packaging/classic-workbench/Build-Native.ps1
pwsh -NoProfile -File packaging/classic-workbench/Stage-Native.ps1
```

Çalıştırılabilir dosya `out/mysql-workbench-native-src/bin/x64/Release_OSS/StarDataParadise.exe` yolundadır. MSBuild, WinForms uygulamasını, C++ ve C++/CLI modüllerini, Python sarmalayıcılarını ve eklentileri 8.0.47 kaynaklarından derler. Paketleme betiği hazırlanmış üçüncü taraf çalışma zamanını ekler.

26.10.0-StarDataParadise canlı kabulünde TLS kimlik doğrulaması, yanlış CA’nın reddi, BIGINT/DECIMAL/BLOB okuması, `ANALYZE` ve rollback geçti. Klasik SQL editörü örnek satırı gösterdi.

Ana ekran, sunucu sürümüyle doğrulanmış StarDataParadise bağlantılarını gösterir. Eski bir bağlantıyı ana ekrana almak için bağlantı yöneticisinde **Test Connection** ile doğrulayın. SQL editörü her oturumda sunucu kimliğini kontrol eder. Windows yerel hizmet taraması `StarDataParadise` ile başlayan hizmetleri arar.

Classic ana sayfası [tasarım kurallarındaki](tasarim/ANA_SAYFA.md) kompakt **Bağlantılar / Modeller / Veri Taşıma** düzenini kullanır. Bağlantı araması profil, kullanıcı ve sunucu alanlarını süzer; seçili profil **Bağlan**, çift tıklama veya Enter ile gerçek SQL oturumunu açar. **Profili düzenle** ve **Bağlantıyı sına** mevcut bağlantı işlemlerini kullanır. Piksel avatarı profil kimliğiyle saklanır; yeniden adlandırma avatarı değiştirmez. **Görünüm** menüsündeki karşılama ve ana sayfa tema tercihleri yeniden başlatmada korunur.

**Modeller**, kayıtlı `.mwb` dosyalarını ve yeni model oluşturmayı açar. EER diyagramındaki yatay araç çubuğunda **Tablo** ve **İlişki ekle** gerçek model araçlarıdır. **Veri Taşıma**, kaynak ve hedef seçimi, tablo eşleştirme, SQL önizleme ve veri aktarımı için mevcut migration motorunu çalıştırır. Önizleme hedefe yazmaz; SQL uygulama ve veri aktarımı ayrı kullanıcı adımlarıdır. Diğer veritabanı türlerinden taşımada kaynağa uygun x64 ODBC sürücüsü gerekir.

Derleme, canlı kabul sonuçları ve native görünümün kapsamı [uygulama kaydında](tasarim/UYGULAMA.md); gerçek model ve aktarım testlerini tekrar çalıştırma adımları [test kılavuzundadır](tests/README.md).

**Yeni bağlantı**, kompakt Genel / TLS / SSL / Gelişmiş formunu kullanır. **Profilleri yönet**, piksel avatarlarını, tek sekme sırasını ve gerçek TLS/SSH ayarlarını gösterir. **Veri Taşıma** yan yana kaynak/hedef seçimiyle başlar; ayrı salt okunur SQL önizlemesi ve **Betiği kaydet…** adımından sonra uygulama seçeneklerine geçer. Bu ekranların ayrıntıları ve kabul sonuçları [bağlantı/taşıma uygulama kaydındadır](tasarim/baglanti-ve-tasima/UYGULAMA.md).

Yerel örnek veritabanı için **Standard (TCP/IP)** bağlantısını `localhost:3197` adresinde açın. **Rescan for Local Servers** bu uç noktayı bulur. Aynı port StarDataParadiseDrive 2.0.2 ve sonraki sürümlerin yerel SDK bağlantılarını da kabul eder. Doğrulanmış StarDataParadise sunucuları eski Classic sürüm uyarısı göstermeden editörü açar.

**Query 1** editöründe `Ctrl+Space` ile SQL önerilerini açın. Boş bir ifadede `CREATE`, `SELECT`, `UPDATE` ve `SET` gibi komutları görebilirsiniz. StarDataParadise bağlantısında `SELECT STAR` ile `STARDATAPARADISE_CAPABILITIES()` ve `SET SESSION sdp` ile `sdp_compatibility_mode` önerilir.

Bağlı Classic Server model komutlarını destekliyorsa `CREATE MO`, `TRA` ve `PRED` sırasıyla `CREATE MODEL USING`, `TRAIN MODEL USING` ve `PREDICT MODEL USING` ifadelerini tamamlar. `USING` sözcüğünden sonra model isteğini tek tırnak içinde JSON nesnesi olarak girin.

## 8.0.43 önizlemesi

`v0.2.0-preview.2`, kaynak yamasındaki özgün Windows satır sonlarını korur.

Yerel masaüstü paketi MySQL Workbench 8.0.43 kaynaklarını kullanır. Karşılama, bağlantılar, menüler, pencere başlıkları ve logo StarDataParadise markasını taşır. SQL editörü yerleşimi klasik kaynakla aynıdır.

Kaynak tabanı `mysql/mysql-workbench` deposunun `8.0.43` etiketi ve `cea23ea2126ff92a61fe8528bc311d4ab978f61d` commit’idir. Önce bu commit’i ayrı bir çalışma ağacına alın. Yama yolunu aşağıdaki komutta değiştirin.

```powershell
git apply --check C:/src/StarDataParadise/packaging/classic-workbench/workbench-8.0.43.patch
git apply C:/src/StarDataParadise/packaging/classic-workbench/workbench-8.0.43.patch
```

Doğrulanan Windows x64 paketi yamalı kaynaklardan derlenen WinForms yürütülebilir dosyası, `mysql.forms.dll` ve `mforms.wr.dll` ile aynı sürümün imzalı Oracle dağıtımındaki diğer bileşenleri kullanır. Tüm upstream çözümü için Visual Studio, C++/CLI, .NET Framework 4.5 ve 4.5.2 başvuru derlemeleri ile upstream geliştirme kitaplıkları gerekir. Derlenen dosyaları 8.0.43 çalışma zamanı ile aynı dizine yerleştirin. Upstream lisans ve bildirim dosyalarını koruyun.

Forms hedefleri derlendikten sonra aynı paketi hazırlamak için bu dizindeki `Build-Desktop.ps1` betiğini kullanın. `-Source` yamalı 8.0.43 çalışma ağacını, `-FrameworkRoot` altında `.NETFramework/v4.5` bulunan başvuru derlemeleri dizinini, `-FormsOutput` iki DLL’in derleme çıktısını, `-Output` ise yeni paket dizinini gösterir. `-OracleRuntime` ile ayrı bir yerel 8.0.43 dağıtım dizini seçilebilir.

Yerel kabulde 26.10.0-StarDataParadise ile TLS kimlik doğrulaması, yanlış CA reddi, BIGINT/DECIMAL/BLOB sorguları, `ANALYZE` ve rollback geçti. Klasik editör bu Server sürümü için uyumluluk uyarısı gösterebilir. Sürüm etiketi kaynak önizlemesine aittir.
