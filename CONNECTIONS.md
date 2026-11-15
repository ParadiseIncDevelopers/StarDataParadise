# StarDataParadise bağlantı yöneticisi

İlk pencere bağlantısız açılır. Yerel bağlantı formu `127.0.0.1:7443` ile başlar. Uzak bağlantıda bir adres girilir. Her iki formda PEM güven paketi ve kullanıcı gerekir. TLS adı boşsa host doğrulanır. Örnek veri seçeneği SQL desteği gerektirir.

Profil adı, host, port, veritabanı tercihi, TLS adı, CA yolu, kullanıcı, ortam (`dev`, `test`, `prod`) ve read-only tercihi düzenlenebilir. Veritabanı ve read-only ayarları profil metadata’sıdır. Yetkileri Server belirler. Test düğmesi gerçek bağlantı açar, doğrular ve sekme eklemeden kapatır. Bağlan düğmesi her oturum için ayrı native handle, uç etiketli sekme ve nesne ağacı düğümü açar.

Windows Credential Manager seçeneği açıkça işaretlendiğinde başarılı bağlantıdan sonra parola yerel kullanıcı kasasına kaydedilebilir. Boş parola alanı ve işaretli seçenek aynı profile ait kaydı okur. Kayıt anahtarı profil adı, uç, kullanıcı, TLS adı ve CA içeriğinin SHA-256 değeriyle ilişkilidir. Kasa erişilemiyorsa kullanıcı parola girebilir ve disk üzerinde plaintext parola yazılmaz.

Çalışma alanı dışa aktarımı yalnız profil alanlarını içeren UTF-8 JSON üretir. İçe aktarım sabit şemayı doğrular ve yalnız alan değerlerini yükler. Ağ, credential store ve profil dosyası işleri GUI iş parçacığının dışında çalışır. Kapalı sekmenin sonuçları yoksayılır.

Server’ın boolean capability’leri kullanılabilir veya unavailable görünür. Native hata verdiğinde kurulmamış modül, yetkisizlik, protokol uyumsuzluğu ve bağlantı kopması ayrı durum metinleriyle gösterilir. Bağlantı yokken health değeri gösterilmez.
