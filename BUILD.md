# StarDataParadise Workbench derleme ve test

Windows x64, C++20, Visual Studio 2026 (v145), Windows SDK 10.0.26100.0, CMake 4.2.2 ve Qt Base 6.11.3 MSVC x64 kullanılır. Workbench Qt Widgets ve Qt Network bileşenlerine dinamik bağlanır. Yerel bağlantılar için native SDK 0.1.0 ve ABI 1 kullanılır.

```powershell
cmake --preset windows-x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.11.3/msvc2022_64" -DSDP_NATIVE_ARCHIVE="C:/packages/StarDataParadiseDLLler-0.1.0-win-x64.zip"
cmake --build --preset windows-x64
ctest --preset windows-x64
```

CMake, Etap 05 ZIP’inin SHA-256 değerini `9a9e7409c3670e38c1a54bca1bcdc2b9cf9e3f68c87286500c745bdbea1cfd4c` ile doğrular. `windeployqt` platform eklentisini ve Qt DLL’lerini hazırlar. Native DLL EXE’nin yanına kopyalanır. CI `ci.ps1 -QtRoot <qt-sdk> -NativeArchive <zip>` ile çalışır.

Visual Studio çözümünde x64 seç, Qt için `QTDIR` ve ZIP için `SDP_NATIVE_ARCHIVE` tanımla. `--smoke-test` bağlantısız pencereyi açıp denetler. `SDP_TEST_SERVER` ve `SDP_TEST_OPENSSL` yapılandırma girdileri iki geçici Server instance’ı ve Windows Credential Manager oturum kaydı kullanan Python entegrasyon testini etkinleştirir.

Qt Base LGPL-3.0 veya ticari lisansla, CMake BSD-3-Clause lisansla kullanılabilir. [Qt lisansları](https://doc.qt.io/qt-6.11/licensing.html). Profil davranışı [CONNECTIONS.md](CONNECTIONS.md) dosyasında.
