# StarDataParadise Workbench derleme ve test

Windows x64, C++20, Visual Studio 2026 (v145), Windows SDK 10.0.26100.0, CMake 4.2.2 ve Qt Base 6.11.3 MSVC x64 kullanılır. Workbench Qt Widgets ve Qt Network bileşenlerine dinamik bağlanır. Yerel bağlantılar için Native SDK 0.4.1 veya 0.4.0 ve ABI 1 kullanılır.

```powershell
cmake --preset windows-x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.11.3/msvc2022_64" -DSDP_NATIVE_ARCHIVE="C:/packages/StarDataParadiseDLLler-0.4.1-win-x64.zip"
cmake --build --preset windows-x64
ctest --preset windows-x64
```

CMake ve Visual Studio, Native ZIP’i sabit SHA-256 ile doğrular. `windeployqt` platform eklentisini ve Qt DLL’lerini hazırlar. Native DLL EXE’nin yanına kopyalanır. CI `ci.ps1 -QtRoot <qt-sdk> -NativeArchive <zip>` ile çalışır.

Visual Studio çözümünde x64 seç, Qt için `QTDIR` ve ZIP için `SDP_NATIVE_ARCHIVE` tanımla. `--smoke-test` bağlantısız pencereyi açıp denetler. `SDP_TEST_SERVER` ve `SDP_TEST_OPENSSL` yapılandırma girdileri iki geçici Server instance’ı ve Windows Credential Manager oturum kaydı kullanan Python entegrasyon testini etkinleştirir.

Qt Base LGPL-3.0 veya ticari lisansla, CMake BSD-3-Clause lisansla kullanılabilir. [Qt lisansları](https://doc.qt.io/qt-6.11/licensing.html). Profil davranışı [CONNECTIONS.md](CONNECTIONS.md) dosyasında.

## Workbench SQL kabulü

CTest pencere ve bağlantı testlerinde sabit Qt SDK'nın offscreen eklentisini yalnız test süreci için seçer. Workbench uygulaması Windows platform eklentisini kendi dağıtım dizininden yükler. `SDP_TEST_SERVER` ile `SDP_TEST_OPENSSL` verilince SQL kabulü iki geçici TLS sunucusunda çalışır.

Çalışma sayfasında sorgu, seçim ve betik çalıştırma, satır başına zaman ve sonuç sınırı, transaction ve savepoint kontrolleri bulunur. İsimli bind değerlerini editör menüsünden açılan JSON alanına gir. Örneğin `{"parameter_types":{"id":{"type":"BIGINT"}},"parameters":{"id":{"t":"i64","v":"7"}}}`. Metin, değer, kimlik ve oturum bilgisi Native SDK üzerinden taşınır.

Otomatik taslak kurtarma bağlanılan profil ve uç noktaya göre çalışır. Kimlik bilgileri, bind alanı, metin veya yorum içeren sorgular otomatik kurtarma dosyasına yazılmaz. SQL dosyasına kaydetme çalışma sayfasının açık dosya işlemidir.

Tamamlanma yanıtı kaybolan yazılarda request token gösterilir. Durum sorgusu aynı hesapla yeniden bağlanabilir. Açık transaction ile sekme kapanırken geri alma onayı gerekir. Yerel işlem süresi sınırı, bağlantının Native SDK zaman aşımıyla birlikte uygulanır.


## Tablo araçları ve PL/SQL Studio

Nesne gezgininde tabloya sağ tıklayarak tasarımcıyı, grid’i veya aktarım sihirbazını aç. Grid düzenlemeleri güvenli bir satır anahtarı ve güncel revizyon ister. Grid görünür sayfayı dışa aktarır. Aktarım sihirbazı tüm tabloyu veya ilk N satırı seçer. CSV ve Typed JSON, NULL ile boş veriyi ayırır ve BLOB taşır. SQL dosyaları çalışma sayfasında incelenir.

Çalışma sayfasının araç menüsünden yeni PL/SQL yordamı oluştur. Bir yordamın gezgin menüsünden düzenlenen kaynağı, IN/OUT çağrısını, testleri veya Java aktarımını aç. Debugger ayrı bir kontrol oturumu açar. Sunucu, yordam testlerini yalnız `routine_test_instance` etkin ve yetkili bir test oturumunda çalıştırır.

Java aktarımında yerel Python 3.12 uygulamasını ve Server 0.4.0 kaynaklarındaki `tools/routine_export` klasörünü seç. Workbench üretici dosyalarını doğrular ve Server parolasını TLS anlık görüntüsünü alan sürecin standart girdisine iletir. Standalone kaynaklar JDK 17 ile Server kapalıyken çalışır. Bağlı kaynak JDBC 0.2.0 ve Gson 2.13.2 kullanır. Yerel derleme ve test ayrı düğmeyle onaylanır.


## History ve Decision Studio

Çalışma sayfasının araç menüsü veya nesne gezgini üzerinden stüdyoyu aç. History, sunucunun izin verdiği revizyonları ve sabit satır kimliklerini gösterir. Kurtarma önizlemesi kaynak ve güncel görüntüyü içerir. Apply sırasında sunucu planı, revizyonu ve güncel kuralları yeniden denetler.

Decision sonuçlarında değerlendirilen ve atlanan koşullar ayrı izlenir. WHATIF ve sürüm etkisi, Server’ın salt okunur simülasyon çağrılarını kullanır. Son API isteği ve DDL önizlemesi stüdyodan kopyalanabilir.


## ML, Dataset ve Job Studio

Dataset Studio, tipli bind değerleriyle bir SELECT snapshot’u oluşturur. Ekrandaki dağılım, NULL oranı ve bölünme sayıları yakalanan satırlara aittir. ML Studio'da model algoritmasını seç, veri seti kimliğini bağla ve eğitim isteğini önizle. Sunucu job kimliğini, değerlendirme metriklerini ve sürüm özetini döndürür.

Job Center, aynı profil için doğrulanan son iş kimliklerini hatırlar ve iş durumunu yeniden Server’dan okur. Duraklatılmış MLP işinde checksum’u doğrula ve yeni job kimliğiyle devam et. Yerel Python kontrolü, uzak MLP modülü ve uzak GPU backend’i ayrı gösterilir.


## Sağlık, yedek ve erişim yönetimi

Instance and Health Center, son ölçüm zamanını ve kanıtı bağlı Server’dan alır. UNKNOWN, o kontrol için doğrulanmış bir sonuç bulunmadığını gösterir. Yerel Windows hizmet durumu ayrı işletim sistemi ekranındadır. Redakte destek özeti yerelde kaydedilebilir. Tam destek paketi Server üzerinde oluşturulur.

Backup and schedules ekranında arşivi önce doğrula, ardından ayrı bir Server hedef adıyla geri yükle. Zamanlama durumunu ve son iş kimliğini Server saklar. Desteklenen iş türleri ve kapalı türler capability yanıtından gösterilir.

SQL betiği ve yeniden eğitim zamanlaması Native 0.4.1 ve ilgili Server yeteneğini gerektirir. SQL zamanlaması en çok 16 SELECT, INSERT, UPDATE veya DELETE ifadesi alır. Server betiği tek işlemde çalıştırır ve sonuçta satır sayılarını gösterir. Yeniden eğitim seçilen değişmez veri setini kullanır ve yeni model sürümünü yayınlar. Etkin model sürümü ayrı yönetilir.

Users and permissions ekranı uygulanacak GRANT veya REVOKE ifadesini onaydan önce gösterir. Etkin tablo erişimi mevcut oturumun metadata yanıtından okunur. Workspace dışa aktarımı bağlantı profillerini içerir. İçe aktarılan SQL projeleri dosyayı yalnız düzenleyicide açar.

**Tools → View update status…** menüsünden ortak güncelleme aracının oluşturduğu yerel JSON kaydını seç. Workbench bağlantı gerektirmeden bileşeni, önceki ve hedef sürümü, geçerli ve bekleyen adımı, geri dönüş sınırını ve yönetici gereksinimini gösterir. Kayıt salt okunur açılır.

## Şema karşılaştırması ve dış kaynak aktarımı

Tools menüsünde etkin çalışma sayfası karşılaştırmanın kaynağıdır. Hedef için ikinci bir Server bağlantısı aç. DDL önizlemesinde yeniden adlandırılan nesneleri eşleştir ve silme adımlarını ayrıca onayla. Workbench hedef metadata sınırını transaction içinde yeniden okur. Görünüm, yordam ve Decision tanımlarında Server’ın sunmadığı ayrıntılar elle inceleme listesinde görünür.

PostgreSQL ve MySQL aktarımında yerel sağlayıcının x64 DLL’ini, kaynak TLS CA sertifikasını ve salt okunur hesabı seç. Workbench, DLL’i yanındaki `StarDataParadiseMigrationSource.exe` sürecinde yükler. Kaynak tabloları ve bağlı foreign key tablolarını birlikte işaretle. Önizleme tip eşlemesini, kaynak kodlama ve zaman dilimini, aktarılabilen satırları ve elle ele alınacak nesneleri gösterir. Mevcut hedef tabloları korunur. Aktarım en çok 5.000 satır ve 8 MiB ile sınırlıdır.

Devam etmek için aynı checkpoint dosyasını ve tablo seçimlerini kullan. Workbench kaynak snapshot checksum’ını yeniden doğrular ve önceden yazılan hedef satırlarını anahtar ve tipli değerleriyle eşleştirir. Checkpoint bağlantı şifrelerini içermez. Test ortamında iki gerçek sağlayıcıyı doğrulamak için CMake’e `SDP_TEST_POSTGRESQL` ve `SDP_TEST_MYSQL` kurulum köklerini de ver.
