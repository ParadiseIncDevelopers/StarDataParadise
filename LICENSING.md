# Workbench licensing / Lisans kapsamı

This repository includes both an independent Qt/wire v1 application and
Classic Workbench based on MySQL Workbench. The following terms apply to
different components; they are not a choice of interchangeable licenses.

| Component | Applicable terms |
| --- | --- |
| Original Qt client in `StarDataParadise/` and support files for that independent client, outside the exclusions below | [StarDataParadise Workbench Free-Use License 1.0](licenses/StarDataParadise-Workbench-Free-Use.txt) |
| `classic-workbench/` and original modifications to its GPL-covered sources | GPL version 2, with the applicable upstream permissions and separately identified component licenses in [License.txt](classic-workbench/License.txt) |
| `packaging/classic-workbench/`, including patches and original build, staging and installation support | GPL version 2 for original contributions and GPL-derived material, with the corresponding upstream terms and separately identified component licenses |
| Tabler-derived UI assets | [Their existing MIT license](classic-workbench/images/ui/tabler-source/LICENSE.txt) |
| Qt, Native SDK, OpenSSL and other dependencies | Their own licenses and distribution notices |

For copyright-controlled StarDataParadise contributions in the identified GPL
scope, the [additional linking permission](licenses/StarDataParadise-GPL-Linking-Exception.txt)
expressly preserves linking with the separately licensed software covered by
the corresponding upstream release's permission. It grants rights only in
those original contributions and does not change third-party terms.

## Original application

The Free-Use License allows no-cost personal and commercial use and
redistribution of the unmodified official Qt application binary with its
notices. It does not grant general source modification or redistribution
rights. This part of the repository is not open-source licensed.

These proprietary terms never apply to the Classic Workbench executable,
GPL-covered source or its derivative works. A product name or renamed
executable does not change the underlying license.

## Classic sources and distribution

Original Classic build/staging/install support is included in the GPL scope
so that it can accompany corresponding source. The proprietary Free-Use
terms apply only to the independent Qt application and its support files.

Preserve [the complete existing upstream license and notices](classic-workbench/License.txt),
all source headers, and separately licensed assets. The 8.0.43 patch uses the
[8.0.43 upstream license](https://github.com/mysql/mysql-workbench/blob/cea23ea2126ff92a61fe8528bc311d4ab978f61d/License.txt);
the current 8.0.47 tree carries its own license in this repository.

When distributing GPL-covered binaries, comply with the applicable GPL
source-availability requirements for the exact distributed version,
including the StarDataParadise modifications and required build material.
An upstream link alone is not a substitute for those obligations.
Upstream additional linking permissions apply only within their stated scope.
The separate licenses of Qt and other redistributed dependencies must also
be satisfied.

No trademark or endorsement rights are granted. File-specific notices and
previously granted rights are preserved. This document describes licensing;
it does not certify a binary distribution's compliance.

## Türkçe özet

Özgün Qt/wire istemcisi, lisans ve telif bildirimleri korunarak değiştirilmeyen
resmî ikilisinin ücretsiz kişisel ve ticari kullanımına izin veren özel
lisans altındadır. Kaynak koduna genel değiştirme ve yeniden dağıtım izni
verilmez. MySQL'den türeyen Classic Workbench ve yamalar ise mevcut GPLv2
kapsamındadır; özgün uygulamanın özel lisansı bunları kısıtlamaz.
