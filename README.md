# StarDataParadise Workbench

StarDataParadise için Windows x64 masaüstü SQL istemcisi. Bağlantı profillerini yönetebilir, sorgu ve betik çalıştırabilir, tablo verisini düzenleyebilir ve sunucunun sunduğu yönetim araçlarını kullanabilirsiniz.

**Güncel yerel uygulama (0.3.1):** [StarDataParadise.exe](out/release/StarDataParadise-v0.3.1/StarDataParadise.exe). Masaüstündeki **StarDataParadise** kısayolu aynı EXE'yi açar. EXE, yanındaki DLL ve destek klasörleriyle birlikte tutulmalıdır. 22 Kasım 2026'da eski/tekrarlanan yerel EXE'ler temizlendi; önceki kabul belgelerindeki çıktı yolları tarihsel kayıttır.

| Çalıştırmak istediğiniz istemci | Bağlantı | Başlangıç |
| --- | --- | --- |
| Qt Workbench | TLS üzerinden StarDataParadise wire v1, örnek port `7443` | [Bağlantı kılavuzu](CONNECTIONS.md) ve [derleme](BUILD.md) |
| Classic Workbench (8.0.47 tabanı) | MySQL Classic TCP, ortak yerel dinleyici için örnek `localhost:3197` | [Kaynak yaması, Windows derlemesi ve paketleme](packaging/classic-workbench/README.md) |

## Tam Classic kaynakları

Classic Workbench 8.0.47 kaynaklarının tamamı [classic-workbench/](classic-workbench/) altında master dalında tutulur. Yerel native master commit'i `b201ed14d7624571975db6a9dfa6dd077d712bf5` ilk tam kaynak yayınının kaydıdır; güncel arayüz değişiklikleri aynı kanonik ağaca eklenir. Derleme bağımlılıkları ve Windows adımları [Classic kılavuzunda](packaging/classic-workbench/README.md) bulunur. [SQL çalışma alanı tasarım galerisi](packaging/classic-workbench/tasarim/sql-calisma-alani/README.md) 41 sayfa taslağı içerir.

## Güncel kaynak sürümü

**v0.3.1**, preview olmayan kararlı sürümdür. OpenAI/Claude anahtarı için hızlı
kurulum, bilgisayardan GGUF importu ve yerel worker durumu/başlatma eklenmiştir.
AI kataloglarının aynı bağlantıda art arda okunması düzeltilmiştir. AI/Knowledge ekranları, kullanıcının
OpenAI/Claude anahtarı veya kendi yerel LLM dosyası, ayrı AI SQL yürütme ekranı
ve normal SQL/PLSQL için typed IntelliSense/hover bu sürümde birlikte bulunur.
Modal işlem düğmeleri ortak koyu petrol yeşilini kullanır. HIST/Git ve yeni
SQL JOB ekranları sonraki etaba ertelendi; tasarım görselleri korunur.
[Ürün sürümü](release.json) · [Sürüm notları](packaging/classic-workbench/SURUM_NOTLARI.md)
· [Önceki veri/DDL ekranlarının kabulü](packaging/classic-workbench/tasarim/sql-calisma-alani/VERI-SEMA-YONETIM.md)

## İlk bağlantı

### Qt Workbench

1. Bir [StarDataParadise Server](https://github.com/ParadiseIncDevelopers/StarDataParadiseServer) örneği başlatın veya mevcut bir wire v1 örneğinin adresini, portunu, kullanıcı adını ve CA PEM dosyasını alın.
2. **Connections → Manage profiles…** menüsünde bu bilgileri girin. Bağlantı adresi sertifikadaki addan farklıysa sertifika adını **TLS name** alanına yazın.
3. **Test** ile bağlantıyı doğrulayın. **Connect** ile bir SQL çalışma sayfası açın. Parolayı yerel Windows Credential Manager’da tutmak için profildeki kasa seçeneğini kullanın.

SQL çalışma sayfası seçili metni veya betiği çalıştırır ve transaction ile savepoint kontrolleri sunar. Nesne gezgininde tabloların yapısını, verisini ve aktarım seçeneklerini açabilirsiniz. PL/SQL, History, Decision, veri kümesi/model, iş, sağlık ve yedek ekranları sunucunun etkin özelliklerine ve hesabınızın yetkilerine göre çalışır. [Çevrimdışı SQL ve PL/SQL başlangıcı](https://github.com/ParadiseIncDevelopers/StarDataParadiseServer/blob/master/docs/coordination/S50_QUICKSTART.md)

### Classic Workbench

[Classic kaynak kılavuzu](packaging/classic-workbench/README.md) MySQL Workbench 8.0.47 yamasını, üçüncü taraf bağımlılıklarını ve Windows paketleme betiklerini açıklar. Ortak yerel dinleyici yapılandırılmışsa **Standard (TCP/IP)** ile `localhost:3197` adresini kullanın. **Test Connection** sunucuyu doğrular. **Query 1** editöründe `Ctrl+Space` ile tamamlamayı açabilirsiniz. [Classic sunucu kurulumu ve uyumluluk](https://github.com/ParadiseIncDevelopers/StarDataParadiseServer/tree/master/compatibility)

## Qt istemcisini derleme

CMake 4.2.2, Visual Studio 2026 C++ (v145), Windows SDK 10.0.26100.0, Qt Base 6.11.3 MSVC x64 ve desteklenen Native SDK ZIP’i gerekir. Aşağıdaki örnekte Qt SDK ve ZIP yollarını kendi ortamınıza göre değiştirin.

```powershell
cmake --preset windows-x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.11.3/msvc2022_64" -DSDP_NATIVE_ARCHIVE="C:/packages/StarDataParadiseDLLler-0.4.1-win-x64.zip"
cmake --build --preset windows-x64
ctest --preset windows-x64
.\out\build\windows-x64\Release\StarDataParadise.exe
```

CMake sürüme sabitlenmiş Native ZIP’in SHA-256 değerini denetler. Örnekteki 0.4.1 arşivinin beklenen özeti `8073941b68c6c037ea646d4cc3f2bcc31445462356c9bd0db513b35cc2378041`. Derleme çalışma zamanı dosyalarını EXE ile hazırlar. Canlı TLS testleri için yapılandırmaya `SDP_TEST_SERVER` ve `SDP_TEST_OPENSSL` yollarını ekleyin. [Visual Studio, test seçenekleri ve stüdyo kılavuzu](BUILD.md) · [Windows kurulum paketleri](https://github.com/ParadiseIncDevelopers/StarDataParadiseServer/tree/master/packaging) · [Sürüm notları](https://github.com/ParadiseIncDevelopers/StarDataParadise/releases)

## Lisans

Bu depo birden fazla lisans kapsamı içerir. Özgün Qt/wire v1 istemcisi,
[Workbench Free-Use License 1.0](licenses/StarDataParadise-Workbench-Free-Use.txt)
ile değiştirilmeyen resmî ikililer için ücretsiz kişisel ve ticari kullanım
sunar; bu, kaynak koduna genel değiştirme ve dağıtım izni veren bir lisans değildir.

MySQL'den türeyen [Classic Workbench](classic-workbench/License.txt) ve
ilgili yamalar ve özgün Classic derleme/kurulum betikleri GPLv2 ile mevcut upstream ek izinleri ve üçüncü taraf
koşullarını korur. Ayrıntılar: [LICENSE](LICENSE) · [Lisans kapsamı](LICENSING.md).
