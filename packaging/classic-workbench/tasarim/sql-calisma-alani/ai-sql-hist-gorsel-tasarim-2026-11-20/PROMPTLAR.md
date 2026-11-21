# Görsel üretim istemleri

Yöntem: yerleşik ImageGen aracıyla her ekran için ayrı bir görüntü üretildi. Kullanıcının paylaştığı tam pencere ve şema gezgini görüntüleri referans alındı; sonraki ekranlarda seçilen ilk Knowledge ekranı veya HIST ağacı da görsel tutarlılık referansı olarak kullanıldı. Görseller gerçek uygulama ekran görüntüleri değildir.

## Ortak istem

> One realistic StarDataParadise Windows desktop product UI mockup, landscape 1920x1020. Use the attached screenshot as strict reference: dark petrol title strip, Turkish menus, compact desktop controls, white editor, mint selected row and thin underline, square corners, Segoe UI, Consolas for SQL; preserve SQL/Veri/Şema/Performans/Yönetim/Araçlar bar, schema explorer and bottom output tabs. Use 20.11.2026 for any dates. Show a single screen, no collage, no decorative marketing cards. Use SQL/PLSQL for knowledge definitions, never YAML. This is a design concept.

Üretici gerçek çıktıları 1717×916 piksel verdi. Aşağıdaki konu istemleri ortak isteme eklendi. İlgili görselde yalnız bir ekran veya modal öne çıkarıldı.

## Ekran istemleri

| No | Konu istemi |
| --- | --- |
| 01 | Extend the schema tree with conversations, Stored Knowledge, behavior, AI models and response schemas; select `siparis_bilgisi` and display its SQL definition and object tabs. |
| 02 | Show a searchable Stored Knowledge table and an object context menu with open, edit, run, dependencies, permissions and delete. |
| 03 | Show a new Knowledge editor with schema/name, READ ONLY, SQL SECURITY INVOKER and SQL definition; show validate and review buttons. |
| 04 | Show a Knowledge parameters/results modal with typed input/output grids, NULL controls and security settings. |
| 05 | Show Knowledge dependencies and revisions for `orders`, `kaynak_disiplini` and `teslimat_gecikmesi`; include validate and version actions. |
| 06 | Show a Knowledge execution modal with typed `p_order_id`, result table and a note that execution does not automatically append context. |
| 07 | Show a versioned Behavior definition and its linked conversations and priorities. |
| 08 | Show a new AI conversation modal with model, functions choice, AUTO APPEND MESSAGE, memory and server-generated ticket note. |
| 09 | Show a conversation inspector with ticket, model, message history, context, memory and run status. |
| 10 | Show conversation memory and a FLUSH MEMORY confirmation that preserves transcript and behavior definitions. |
| 11 | Show a JSON response schema editor and a sample validation error with numeric code 123456 and path `$.status`. |
| 12 | Show a role permissions and approval modal for a procedure with EXECUTE/APPROVE matrix and SQL preview. |
| 13 | Show normal SQL using `EXCLUDE`/`REPLACE` with column completion and contextual help; no COMPACT mode. |
| 14 | Show a `QUALIFY` query with window-function suggestions and contextual help. |
| 15 | Show an incomplete `FROM ... \|>` pipeline with `WHERE`, `EXTEND`, `AGGREGATE` and stage completion; no semicolon before the incomplete stage. |
| 16 | Show `INSERT ... BY NAME` and a source-to-target column-name matching modal. |
| 17 | Show a short procedure using `LET` and `WHEN ... DO`, local-variable completion and correct help text. |
| 18 | Show a procedure editor using query `FOR`, a typed row variable, and `END FOR`. |
| 19 | Show a parameterized single-expression function and its execution/result modal. |
| 20 | Show `SHOW EXPANDED` with original and expanded SQL side by side and a note that the query is not executed. |
| 21 | Show a SQL error with underlined source location, numeric code 123456 and detailed diagnostic modal. |
| 22 | Show parameter binding with typed values, context suggestions and a bind-and-run action. |
| 23 | Show a compact HIST tab adjacent to query history with recent revisions and an expand action. |
| 24 | Show an expanded Git-like revision graph with main/development branches, a two-parent merge and selected revision details. |
| 25 | Show revision details with data changes, related jobs and external effects in distinguishable sections. |
| 26 | Show a side-by-side UPDATE diff for loan 9001 with unchanged ID/member and changed due date. |
| 27 | Show a REVERT preview for duplicate loan transaction c102, preserving the valid loan and listing affected counter/job. |
| 28 | Show a RESTORE preview comparing current and target historical record values. |
| 29 | Show a stale-plan error with affected record, expected/current revisions and an action to regenerate the preview. |
| 30 | Show branch/tag management, creation and switch controls; branch switch applies only to the current connection. |
| 31 | Show remote management and FETCH with credential reference and TLS, noting FETCH does not change local live data. |
| 32 | Show a three-way merge conflict with common ancestor, local target and remote source; no change applies before all conflicts are resolved and the plan validated. |
| 33 | Show HIST sync/job status, direction, interval, pause and retry details; a conflict-paused relationship has no scheduled retry. |
| 34 | Show the HIST event stream with database commit status separate from consumer processing, ACK and expired cursor warning. |

## Seçilen düzeltmeler

- 01 için ilk taslak YAML ürettiği için SQL tanımlı görsel yeniden üretildi.
- 15 için eksik pipeline aşamasından önce noktalı virgül bulunmayan son sürüm seçildi.
- 17'de WHEN açıklaması DO'nun zorunlu olduğu biçimde düzeltildi.
- 26, INSERT ile UPDATE farkının karışmaması için basit bir UPDATE örneğiyle yeniden üretildi.
- 27'de seçili c102 işleminin türü INSERT olarak düzeltildi.
- 32'de kısmi uygulama izlenimi veren açıklama, planın tümünün doğrulanması gerektiği biçimde düzeltildi.
- 33'te çakışma nedeniyle durmuş ilişkide sonraki otomatik retry zamanı kaldırıldı.

## Son kullanıcı düzeltmeleri - görsel revizyon istemleri

- Başlık ve yan yana bilgi parçalarında ` - ` kullan; modal başlığını `Knowledge - Parametreler ve sonuç` olarak göster. Nokta ve orta noktayı görsel ayraç yapma.
- Knowledge bağımlılıklarını üç bağlı düğümlü diyagram, iki sürümü yatay zaman çizgisi olarak göster; düğüm ve yüzeyleri Classic arayüzündeki gibi düz köşeli tut.
- Yeni AI konuşması modaline `Oluşturduktan sonra SQL sekmesini aç` seçeneğini ekle. Oluşturma sonrası ayrı SQL editöründe `START AI CONVERSATION`, `APPEND PROMPT`, `COMMIT` ve `FINISH`, bağlı değerler ve sonuç paneli göster; sekmeyi açarken otomatik yürütme yapma.
- Konuşma ayrıntısına `SQL ile çalıştır` geçişi ekle. Yetki/onay modalini #043133 petrol, #E7EEEF gri, #DDEBE4 nane ve düz kenarlarla mevcut tasarım diline uydur.
- HIST'in alt panelini ve ana revision görünümünü tablo yerine satırlara hizalı bağlantılı Git grafiği ve kısa işlem özetleri olarak göster. REVERT, RESTORE, eski plan ve dal yönetimi pencerelerinin arka planlarını aynı akışa uyarla.
