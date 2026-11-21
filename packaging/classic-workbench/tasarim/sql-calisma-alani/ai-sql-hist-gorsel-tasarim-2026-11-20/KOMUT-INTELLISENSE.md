# SQL renklendirme ve IntelliSense tasarım envanteri

Bu belge yeni arayüz için **hedef** sözdizimi ve yardım kapsamıdır. Burada listelenmesi bir komutun mevcut sunucuda uygulandığı anlamına gelmez. Kaynak sözleşmeleri: kısa SQL/PLSQL, AI SQL ve HIST için kullanıcı tarafından sağlanan üç metin. Kısa SQL'e `COMPACT` adıyla komut, mod veya oturum ayarı eklenmez.

**Güncel release kapsamı:** kullanıcının 21 Kasım yönlendirmesiyle SQL git/HIST ve
yeni public SQL JOB sistemi sonraya alındı. Bu sayfadaki ilgili tasarım satırları
gelecek çalışma referansıdır; release editöründe öneri, özel renk veya native
validation bypass sağlamaz. Mevcut sorgu geçmişi ile MySQL HISTORY/HISTOGRAM
davranışları korunur. Aktif kapsam AI ve kısa SQL/PLSQL'dir.

21 Kasım kapsam kararı: HIST/SQL git ve yeni kullanıcı SQL JOB komutları bu release'ten çıkarıldı; aşağıdaki ilgili bölümler gelecek tasarım envanteridir. Aktif editör desteği AI ve SQL/PLSQL için doğrulanan yeteneklerle sınırlıdır. Mevcut Compute araçları korunur.

## Renk rolleri

Mevcut Classic editöründeki `classic-workbench/res/wbdata/code_editor.xml` temel alınır. Açık tema için örnekler:

| Öğe | Mevcut rol ve örnek renk | Örnek |
| --- | --- | --- |
| Anahtar sözcükler | Mavi, kalın `#007FBF` | `SELECT`, `KNOWLEDGE`, `HIST`, `QUALIFY` |
| Metin | Turuncu `#DD7A00` | `'KRITIK'`, `'development'` |
| Sayı | Turuncu `#CC6C00` | `42`, `300` |
| Değişkenler | Yeşil `#63BF8D` | `@ticket`, `:p_order_id` |
| Fonksiyon ve yordam sözcükleri | Mevcut fonksiyon rengi `#7D7D63` | `ROW_NUMBER`, `COUNT` |
| Yorum | Mevcut yorum rengi `#0987CB` | `-- açıklama` |
| Operatör | Mevcut operatör stili | `\|>`, `=`, `*` |
| Hata | Tema uyumlu işaret ve metin | Alt çizgi, satır/sütun, örnek kod `123456` |

Koyu tema ve erişilebilirlik ayarları kendi renkleriyle korunur. Sözcükler yalnız doğru dil bağlamında biçimlendirilir; yorum, metin veya tırnaklı nesne adının içi komut sanılmaz. Renk tek başına hata veya durum göstergesi değildir.

## Ortak IntelliSense davranışı

- `Ctrl+Space` ve yazım bağlamı ile anahtar sözcük, örnek komut ve uygun sonraki seçenekler önerilir.
- Öneri satırında tür/ikon, kısa açıklama, gerektiğinde parametre tipi ve varsayılan değer gösterilir. Sekme/Enter ile seçim ve Escape ile kapatma mevcut editör davranışını izler.
- Şema, tablo, sütun, model, knowledge, behavior, conversation, response schema, branch ve revision adları ilgili bağlamda önerilir. Sunucudan alınan nesneler kullanıcının görünürlük/yetkilerine tabidir.
- `:name` bağlı parametre, `@name` betik/oturum değişkeni olarak ayrılır; yerel PL/SQL değişkenleri ve `:satir.alan` üyeleri doğru kapsamda gösterilir.
- Yorumlarda ve metin sabitlerinde komut önerisi açılmaz. Eksik girilmiş ifade, iç içe blok ve imleç konumu dikkate alınır.
- Özellik önerileri bağlı sunucunun yeteneğine göre sunulur; desteklenmeyen veya kurulmamış özellikler açıkça belirtilir. Renkli görünüm kendi başına yürütme desteği anlamına gelmez.
- Sağdaki **SQL yardımı ve parçalar** seçili sözcük için kısa söz dizimi, örnek ve ilgili kısıtları sunar. Örnekler parametrik kalır.

## AI SQL, Knowledge ve Behavior

| Komut veya sözcük grubu | Bağlamsal tamamlama / yardım |
| --- | --- |
| `CREATE AI CONVERSATION`, `START AI CONVERSATION`, `ALTER CONVERSATION`, `SHOW`, `DESCRIBE`, `DROP` | Görünür konuşmalar, model adı, ticket/ad hedefi, ayarlar ve nesne yönetimi |
| `USING MODEL`, `WITH FUNCTIONS`, `NO FUNCTIONS`, `AUTO APPEND MESSAGE`, `MEMORY PERSISTENT` | Model yetenekleri ve birbiriyle uyumlu ayarlar |
| `APPEND PROMPT`, `APPEND CONTEXT`, `APPEND RESPONSE` | Parametreler, context/knowledge adı, response biçimi ve capture hedefi |
| `SET MEMORY`, `SHOW MEMORY`, `FLUSH MEMORY`, `PERSISTENT`, `TEMPORARY` | Bellek anahtarı, kapsam ve commit/rollback açıklaması |
| `CREATE BEHAVIOR`, `INSERT BEHAVIOR`, `DELETE BEHAVIOR`, `VERSION`, `PRIORITY` | Yetkili behavior ve sürümleri, konuşma bağlantısı ve öncelik |
| `CREATE KNOWLEDGE`, `ALTER KNOWLEDGE`, `EXECUTE KNOWLEDGE` | Nesne adı, tipli parametreler, dönüş şeması ve bağlı kaynaklar |
| `VALIDATE`, `EXPLAIN`, `REFRESH`, `INVALIDATE`, `SEARCH KNOWLEDGE` | İlgili Knowledge, destek/yetki durumu ve işlem açıklaması |
| `RETURNS TABLE`, `READ ONLY`, `SQL SECURITY INVOKER`, `INCLUDING BEHAVIORS` | Sütun/tip, güvenlik ve açık behavior bağımlılıkları |
| `CREATE RESPONSE SCHEMA`, `AS JSON`, `WITH (...)` | Şema/sürüm, uygun seçenekler, tür ve desteklenen değerler |
| `CREATE MODEL`, `TRAIN MODEL`, `PREDICT MODEL` | Mevcut klasik model komutları korunarak model, algoritma, sürüm ve veri kaynağı önerileri |
| `COMMIT`, `COMMIT ASYNC`, `DESCRIBE RUN`, `FETCH RESPONSE`, `CANCEL RUN` | Request key, timeout, run kimliği ve sonuç alma; bekleme/iptal durumları ayrı |
| `GRANT`, `REVOKE`, `REQUIRE APPROVAL`, `PREPARE/APPROVE/DENY/EXECUTE ACTION` | Nesne türü, rol, yetki ve onay bağlamı |

Örnek response seçenekleri: `encoding`, `schema`, `schema_version`, `pretty`, `max_output_bytes`, `max_output_tokens`, `stream`, `provider_timeout_ms`, `run_timeout_ms`, `max_attempts`, `retry_backoff_ms`, `temperature`, `top_p`, `seed`, `stop`. Öneriler komut/format/model yeteneğine göre süzülür; bilinmeyen veya uyumsuz seçenekler görünür tanı üretir.

## SQL ve PL/SQL

| Komut veya öğe | Bağlamsal tamamlama / yardım |
| --- | --- |
| `SELECT * EXCLUDE (...)` | Kapsamdaki kaynak sütunları; bulunmayan veya tekrar hedeflenen adlar için tanı |
| `REPLACE (... AS ad)` | Kalan çıktı adları, ifade ve sonuç türü |
| `QUALIFY`, `OVER`, `PARTITION BY`, `ROW_NUMBER`, `RANK`, `DENSE_RANK` | Pencere ifadesi, alias ve değerlendirme sırası açıklaması |
| `FROM ... \|> ...` | `WHERE`, `SELECT`, `EXTEND`, `JOIN`, `AGGREGATE`, `ORDER BY`, `LIMIT`; aşamanın görünür sütunları |
| `INSERT ... BY NAME` | Kaynak çıktı ve hedef sütun adları; DEFAULT ve üretilmiş sütun bilgisi |
| `LET`, `WHEN ... DO`, `FOR ... IN ... DO ... END FOR` | Yerel değişken, tip, kapsam ve satır alanları |
| `CREATE PROCEDURE ... AS ... END`, `CREATE FUNCTION ... AS RETURN` | Parametre yönü/türü, gövde sınırı ve dönüş türü |
| `SHOW EXPANDED` | Sorgu, yordam veya fonksiyon adı; görünümün yürütme yapmadığı bilgisi |
| `GROUP BY ALL`, `ORDER BY ALL` | Ayrı değerlendirme kapsamındaki adaylar; destek kesinleşmeden mevcut özellik gibi gösterilmez |
| `SET ENQUEUE JOB`, `FINISH JOB`, `ON FAILURE RETRY`, `BACKOFF` | Ertelenen public SQL JOB tasarımı; bu release'de önerilmez |

Örnek aşamalı sorgu:

```sql
FROM satislar
|> WHERE zaman >= ? AND zaman < ?
|> EXTEND adet * birim_fiyat AS brut
|> AGGREGATE SUM(brut) AS toplam GROUP BY sube_id
|> ORDER BY toplam DESC;
```

## HIST — gelecek etap

| Komut | Bağlamsal tamamlama / yardım |
| --- | --- |
| `HIST LOG`, `STATUS`, `SHOW`, `DIFF` | Erişilebilir revision, tablo, filtre, sayfalama ve karşılaştırma uçları |
| `HIST REVERT`, `RESTORE`, `PREVIEW`, `APPLY` | Hedef revision/kayıt, önizleme ve doğrulanmış plan kimliği |
| `HIST BRANCH`, `SWITCH`, `TAG` | Dal/etiket adı ve revision; SWITCH'in bağlantı kapsamı |
| `HIST REMOTE ADD`, `FETCH` | Remote adı, yapılandırılmış uç nokta ve yetki/bağlantı durumu |
| `HIST MERGE`, `CONFLICTS` | Kaynak/hedef dal, ortak ata, çözülmemiş çakışmalar ve plan |
| `HIST SYNC`, `INTERVAL`, `ON CONFLICT PAUSE` | Remote, yön, durum, saniye birimli aralık ve job seçenekleri |
| `HIST COMMIT MESSAGE` | Yalnız açık transaction bağlamında açıklama ile tamamlama |

Önizleme veri değiştirmez. Uygulama atomiktir ve plan/izinler yeniden doğrulanır. Çakışmada duran sync ilişkisi, sorun çözülmeden otomatik yeniden denenmez. Görseldeki `123456` yalnız gösterim örneğidir; gerçek hata tahsisi ayrıca yapılacaktır.

## Gerçek editör kabulü — 21 Kasım 2026

Bu bölüm yukarıdaki hedef listesinden ayrıdır. Güncel AI+SQL kapsamıyla native
parser DLL üzerinde 140 tamamlama/hover kontrolü ve 12 gerçek statement splitter
senaryosu geçti. Son exact13 turunda parser, forms ve gerçek syntax consumer
db.mysql.parser modülü yeniden derlendi; wbpublic/wbprivate önceki turda geçti. Canlı sunucuya bağlı son UI kabulü release
staging sırasında ayrıca yapılır.

- Anahtar kelime listesi gerçek Scintilla editörüne bağlıdır. Öneriler lexer'ın
  yorum, metin, quoted identifier, imleç ve Unicode konumlarını kullanır.
- Action ve role ACL profilleri ayrı `native_language.ai_action_v1` /
  `ai_role_acl_v1` capability değerleriyle açılır. AI core tek başına
  APPROVE/DENY/EXECUTE ACTION veya custom role GRANT önerisi/validation bypass
  açmaz. Eski HIST bit'i kullanılmaz; ordinary `EXECUTE action` prepared
  statement adı ve `ON model TO role` identifier semantiği korunur.
- `DESCRIBE ACTION`, `PREPARE ACTION FOR TOOL CALL` ve ID sonucu, mevcut native
  Action profiline bağlıdır. `EXECUTE AI TOOL CALL` ile conversation
  `WITH FUNCTIONS (KNOWLEDGE ..., FUNCTION ..., PROCEDURE ...)` explicit araç
  seçenekleri release'in konservatif AI+Action tam profiline bağlıdır; yeni
  editor bit'i eklenmez. Bare `WITH FUNCTIONS` core AI inference seçeneği
  korunur. Knowledge adları aynı ACL görünür cache'den tamamlanır.
  `TOOL` mevcut Scintilla keyword kümesine eklendi. Yeni global lexer reserved
  sözcüğü veya ayrı COMPACT modu oluşturulmadı.
- Parametre, LET ve DECLARE yerel isimleri açık blok/FOR kapsamında tamamlanır.
  Inner shadowing ve END/FOR sonu dikkate alınır. `:yerel` hover'ı açıkça
  belirtilmiş SQL türünü gösterir; inferred tür sunucu derleyicisine bırakılır.
- `:satir.alan` için SELECT alias'ları veya gerçek source column cache'i
  kullanılır. Knowledge source yıldızının tipli alanları native DESCRIBE
  metadata'sından gelir; AI capability kapalıyken bu metadata önerilmez.
  UNION çıktı adları ilk operanddan alınır. CTE, derived kaynak veya pipeline
  sonucu için fiziksel input tablosundan yanlış alan şeması türetilmez.
- `EXECUTE KNOWLEDGE`, `FROM KNOWLEDGE`, `APPEND CONTEXT FROM KNOWLEDGE`
  imzaları görünür nesnenin native DESCRIBE parametre/sonuç türlerinden üretilir.
  Hover ve `(` / `,` yazımındaki calltip, cache kullanır; provider çağırmaz.
  Schema refresh başına ilk32 Knowledge için metadata alınır, kalan nesne adları
  görünür kalır. Alınamayan metadata yerine imza veya tür uydurulmaz.
- Yeni AS/END ve AS RETURN gövdeleri mevcut delimiter kurallarıyla tek SQL
  statement olarak gönderilir. Eski ANTLR'ın bilmediği doğrulanmış native
  aileler için yürütme doğrulaması sunucuya bırakılır. Yorum/metin hataları bu
  yolla gizlenmez. Renklendirme veya snippet tek başına backend desteği değildir.
- Native `FULL [OUTER] JOIN` eski ANTLR grammar'ının yanlış underline'ını
  kaldırır; çalıştırma sırasında gerçek sunucu AST/planner doğrular. Ordinary
  `full` isimleri ve quoted metinler bu eşlemeye girmez. DESCRIBE ACTION için
  gerçek lexer'ın tek token olarak verdiği `@id` biçimi de doğrulanmıştır;
  ordinary `DESCRIBE action id` tablo/sütun yolu korunur.

Test runtime: `out/classic-language-typed-native-acceptance`.
