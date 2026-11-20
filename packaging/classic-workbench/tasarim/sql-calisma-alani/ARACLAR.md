# Classic ürün araçları

19 Kasım 2026. Araçlar alanı etkin bağlantının içine dock edilen tek görünümde Veri geçmişi, Decision kuralları, CPU modelleri, Compute işleri ve Doğrulanmış yedekler sayfalarını açar. Her sayfa gerçek veri kaynağını kullanır. Kurulum veya yetki eksikliği sonuç/ayrıntı alanında sunucunun hatasıyla görünür.

## Bağlanan işlemler

| Sayfa | Çalışan yol |
| --- | --- |
| Veri geçmişi | `information_schema.ROUTINES` üzerinden günlük bulma; `history_log`, `history_read`, `history_diff`; 1–256 kayıtlık sayfa; restore preview JSON; gerçek plan/token ile yeni sorgu sekmesine apply SQL’i açma |
| Decision | UTF-8 kaynak dosyası ve düzenlenebilir kural alanı; güncel sağlayıcının tüm kural setini tek trigger’a çevirme; gerçek SQL’i inceleme ve kaydetme |
| CPU modelleri | Classic SQL kayıt ve sürümleri; CREATE/TRAIN/PREDICT SQL’ini yeni sorgu tamponuna açma; ayrı CPU runner sağlayıcısında gerçek eğitim, tahmin, etkinleştirme ve rollback yardımcıları |
| Compute | Mevcut hesabın kuyruk ve UTC zamanlama kayıtlarını okuma; idempotent token ile submit/schedule; cancel isteği ve zamanlama durdurma; kurulum/yükseltme |
| Yedekler | Mantıksal/fiziksel manifesti okuma; sağlayıcının tam SHA-256 denetimi; fiziksel create ve yeni hedefe restore, bağımsız geçici Server probe’u dahil |

Yazma eylemleri bağlantı ve işlem ayrıntısını gösteren inceleme penceresinden sonra çalışır. History apply ve Classic model SQL’i sorgu editörüne gider; yürütme mevcut SQL editörü komutuyla yapılır. Bu tamponlar kullanıcının sorgu transaction’ını izler. Diğer işletim yardımcıları ayrı yönetim bağlantısında autocommit önkoşulunu kontrol eder. Compute yazma işlemleri sağlayıcı sözleşmesindeki TLS önkoşulunu da kontrol eder.

## Sağlayıcı ayrımları

Classic SQL model kayıtları `sdp_model_definitions` ve `sdp_model_versions` tablolarındadır. Sürüm belirtilmeyen tahmin son sürümü seçer. Hesap bazlı CPU runner kayıtları `model_versions` ve `active_models` tablolarını kullanır; etkinleştirme ve rollback bu sağlayıcıya aittir. Arayüz sağlayıcı seçimine göre ilgili kontrolleri açar.

Compute işi göndermek worker başlatmaz. İş durumu gerçek kuyruk kaydından okunur; bağımsız worker’ın çalışması gerekir. Zamanlamalar günlük yerel saat yerine mevcut sağlayıcının 1–86.400 saniyelik UTC aralıklarını kullanır.

Mantıksal arşivin create/restore işlemi MySQL Shell `util.dump_schemas`/`util.load_dump` çalışma zamanına bağlıdır. Workbench’in yerleşik Python’unda bu çalışma zamanı bulunmadığı için bu iki eylem bu arayüzde sunulmaz. Yerleşik SQL içe/dışa aktarma kendi native aracında kalır. Mantıksal arşiv checksum doğrulaması Workbench içinde çalışır.

Fiziksel yedekleme için Çalışma zamanı sekmesinde gerçek Server/Classic istemci dosyaları, basedir/plugin dizini, kaynak CA ve gerektiğinde istemci seçenek dosyası seçilir. Bu bölüm dar pencerede kaydırılır. Yeni yedek için mevcut arşiv kökü seçildiğinde yeni UUID alt klasörü hazırlanır. Parola kopyalanmaz veya komut satırına eklenmez. Oluşturma/restore arka planda yürütülür; sağlayıcının yerel yol, manifest, Server kimliği, tablo ve hedef koşulları uygulanır. Arşiv işi sürerken araç görünümü kapanmaz.

## Kaynak ve paketleme

Giriş modülü `classic-workbench/modules/wb.utils/sdp_tools_grt.py`, SQL/GRT adaptörü `sdp_tools_backend.py` dosyasıdır. `sdp_compat_*.py` dosyaları Server deposunun güncel aşağıdaki kaynaklarından alınan sağlayıcı kopyalarıdır. İş kuralları UI içinde yeniden yazılmadı.

- `compatibility/history/README.md` ve yordam sözleşmesi
- `compatibility/decision/provision.py`
- `compatibility/ml/cpu_model.py`
- `compatibility/jobs/compute_jobs.py`
- `compatibility/backup/logical_backup.py`
- `compatibility/backup/physical_backup.py`

Yedi Python modülü CMake kurulum listesinde ve Windows MSI dosya listesinde bulunur. Windows `modules/PrepareOutputDir.cmd` içindeki mevcut `wb.utils` kopyalama adımı taşınabilir çıktıya da alır.

## Denetim

`packaging/classic-workbench/tests/test_sdp_tools.py` içindeki 12 sözleşme testi SQL parametre bağlama, tırnak ve yorum içindeki soru işaretleri, sayısal hassasiyet, NULL ve metin ayrımı, History sınırları, tam Decision seti ve gerçek manifest üzerinde dosya değiştirme reddini denetler. Sunucu bağlantısı veya kullanıcı kimlik bilgisi kullanmaz.

`tools_gui_probe.py` gerçek native GRT/mforms çalışma zamanında beş sayfanın oluşturulmasını, History SQL’ini, native sonuç tablosunu, Decision üretimini ve sağlayıcı geçişini izole bir configdir ile kontrol eder. Canlı sunucu yazma işlemlerinin kabulü ayrıca geçici bir Server fixture’ı gerektirir.

Son native probe beş sayfa, altı plugin girişi, gerçek sonuç tablosu, tam Decision üretimi, eski önizlemenin geçersizleşmesi ve model sağlayıcısı geçişi için geçti. Kanıt `out/classic-workspace-ui/tools-native-result.json` dosyasındadır.

Bağlı yeni GUI oturumunda `tools_readonly_probe.py` SELECT/SHOW ile çalıştırıldı. Native NULL ile boş metin ayrımı, en büyük unsigned BIGINT, 38 basamaklı DECIMAL hassasiyeti ve beş sayfanın gerçek bağlantıya dock edilmesi geçti. Model kayıt metadata’sı ve TLS durumu okundu. Sunucu yazma işlemi yapılmadı. Kanıt `out/classic-workspace-ui/tools-readonly-result.json` dosyasındadır.

İlk CLI denemelerinde başlangıç bağlantısı henüz kullanılabilir değildi ve farklı masaüstü/sandbox bağlamı yeni bir boş instance açtı. Bağlı normal masaüstü instance’ına aynı configdir ile script iletimi ve asenkron callback’in tamamlanmasının beklenmesi bu test koşucusu sorununu giderdi. Eski JSON dosyası henüz güncellenmeden okunmuştu; son dosyanın `ok: true` sonucu doğrulandı. Core bağlantı katmanında bu nedenle bir port değişikliği yapılmadı.
