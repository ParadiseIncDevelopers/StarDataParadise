# StarDataParadise Workbench

Bu depo bağımsız bir Visual Studio çözümüdür. CMake ve test çıktıları depo kökündeki ignore edilen `out/` dizinine gider. Kaynak, yerel ayar ve kullanıcı değişikliklerini koru.

Ortak proje kapsamı ve etap kayıtları `StarDataParadiseServer/docs/coordination/` altındadır. Uygulanacak değişiklik için geçerli kullanıcı isteğini ve ilgili etap kaydını oku. Çalıştırılan testleri, başarısızlıkları ve eksik önkoşulları kaydet.

Port seçiminde kayıtlı kullanıcı tercihini koru. Mevcut bağlantı profili ve ilgili sunucunun kalıcı yapılandırması esas alınır; seçilmiş 3197, 3334 veya başka bir portu varsayılanla ya da geçici test portuyla değiştirme. İlk kurulumda henüz seçim yoksa varsayılan önerilebilir. Güncelleme, yeniden başlatma ve EXE tesliminde aynı sunucu için profil, başlatıcı ve worker portlarının uyumunu doğrula. İzole testlerin geçici portlarını gerçek kullanıcı profiline yazma.
