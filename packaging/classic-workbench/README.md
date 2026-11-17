# Klasik Windows Workbench 0.2.0 önizlemesi

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
