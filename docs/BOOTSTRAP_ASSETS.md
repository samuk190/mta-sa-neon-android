# Inventário de assets de bootstrap da engine (P1A.4)

**Data:** 2026-11-10 · **Método:** somente leitura, sem dispositivo.
**Fontes cruzadas:**
- `assetfile.txt` do APK protótipo (índice de bootstrap da engine, 120 entradas declaradas / 120 listadas) — `mta_decompile_android_progress/descompile_mta_launcher_mobile/extracted_apk/assets/`
- APK original MobileSA 2.10 (`MobileSA-release-216c1e7f.apk`, 91.663.260 bytes)
- Manifesto CDN `manifest_android_mta.json` (rev. consultada em 2026-11-10: 264 arquivos, 789.149.378 bytes, gerado 2026-11-06)

O APK auditado inicialmente no Plano 22 não tinha assets. A execução abaixo adicionou o bootstrap e, na validação atual, o APK contém o catálogo regenerado e o reparo da textura de carregamento.

---

## 1. Classificação das 120 entradas do `assetfile.txt`

| Classe | Qtd | Descrição | Ação |
|---|---|---|---|
| **A — No APK protótipo** | 27 | `scache*.txt`, `audio/android/NAV_*.ogg`, `data/*.cfg` (touch layouts), `data/fonts.dat`, `data/script/mainV1.scm` + `scriptv1.img`, `assetfile.txt` | Replicar no nosso APK `assets/` (fonte: protótipo) ou publicar no CDN |
| **B — Só no CDN** | 1 | `data/handling.cfg` (minúsculo; engine pede `DATA/HANDLING.CFG` — resolver via case-aliases) | Já coberto pela geração CDN instalada |
| **C — Indexadas, ausentes em TUDO** | 92 | `socialclub/*` (54), `Fonts/*.TTF` marca R* (12), `images/*` incl. `Splash.png`/`RockstarIntro.png` (10), `xml/*` (8: app/audio/car/item/part_data, color_palete), `audio/audio_data.xml`, `json/socialClubAssets.json`, `social club/…` | **Não-bloqueantes**: também NÃO existem no APK MobileSA 2.10 original (verificado por listagem ZIP). O protótipo chegou ao menu R* sem eles. Documentar tolerância; não fabricar substitutos |

> **Evidência C:** `unzip -l MobileSA-release-216c1e7f.apk | grep -icE "assets/(images|xml|socialclub|json)/|audio_data.xml|BRADLEY|PRICEDOWN"` → `0`.

## 2. Diretórios extras (fora do índice, usados pela engine)

| Dir | Tamanho | Crítico? | Destino proposto |
|---|---|---|---|
| `Text/*.gxt` (7 idiomas) | 8,7 MiB | **SIM** — crash nativo `CText::Load`/`OS_FileRead` comprovado sem eles (root CHANGELOG 0.2.1) | `american.gxt` (1,3 MiB) no APK `assets/`; demais idiomas no CDN (opcional) |
| `Textures/` (fonts + JapaneseFont/RussianFont .met/.png) | 0,3 MiB | **SIM** (fontes do engine) | APK `assets/` |
| `cgui/` (imagens UI mobile) | 7,9 MiB | Provável (UI touch nativa) — validar no boot | CDN (revisão nova) |
| `fonts/` (25 TTF genéricos) | ~21 MiB | Parcial — engine usa subconjunto | CDN; auditar subconjunto real antes de embutir |
| `ac_modules/` (anti-cheat R*) | 4,0 MiB | **NÃO** — escopo próprio: nosso `AntiCheatValidator` substitui | Fora do port |
| `data/` do protótipo (script/cfg) | 3,7 MiB | Duplicado do CDN (geração instalada é autoritativa) | Não embutir; manter no CDN |

## 3. Itens pendentes conhecidos

1. **`texdb/menu/menu.pvr.dat` corrompido** — no CDN com SHA-256 `99a042f5…`; 12 bytes descompactados faltando no stream `mobilesc0` (crash `CLoadingScreen::LoadSplashes → RLEDecompress` no S25). Corrigir/regenerar o asset e publicar revisão nova; **bypass de splash não é critério de aceite** (Plano 22 §2).
2. **GXT ausente do CDN e do APK** — o manifesto (264) não lista GXT; sem `Text/`, o boot morre em `CText::Load`.
3. **Case-aliases** — engine abre `DATA/HANDLING.CFG`, `PEDSTATS.DAT` etc.; reutilizar a abordagem `EngineCaseAliases` do protótipo (aliases privados maiúsculos/minúsculos sem duplicar arquivos).
4. **`assetfile.txt` deve ser regenerado** com os caminhos reais do empacotamento escolhido (APK assets + geração CDN), como feito no protótipo (0.2.1).

## 4. Decisão de empacotamento (proposta)

```
APK assets/               (pequeno, boot-critical)
├── assetfile.txt         (regenerado)
├── scache*.txt           (16 KB)
├── Text/american.gxt     (1,3 MiB)
├── Textures/             (0,3 MiB)
└── audio/android/*.ogg   (sons de navegação UI)

CDN game_files_android_mta/   (geração versionada por SHA-256)
├── (264 atuais)
├── cgui/                 (7,9 MiB)
├── fonts/ (subconjunto auditado)
└── Text/<demais idiomas> (opcional)
```

Critério: APK enxuto (< ~2 MiB de assets próprios); tudo que é grande/versionável vai para o ciclo do manifesto (Regra 5: regenerar manifesto → sync Hugging Face → deploy Oracle).

## 5. Executado (2026-11-10)

- **Subset boot-critical embutido no APK** (`app/src/main/assets/`, origem: protótipo MobileSA 2.10 extraído, 5,2 MiB → APK debug 13.350.855 bytes):
  - `assetfile.txt` (verbatim, 120 entradas — layout híbrido idêntico ao provado pelo protótipo)
  - `scache.txt`, `scache_small.txt`, `scache_small_low.txt`
  - `audio/android/NAV_DOWN.ogg`, `NAV_UP.ogg`
  - `data/script/mainV1.scm` (3,2 MiB — **gap do CDN**, não está no manifesto) + `scriptv1.img` (duplicado de propósito: o protótipo lia do root de assets)
  - `Text/american.gxt` (1,3 MiB)
  - `Textures/fonts/` (Japanese/Russian .met+.png)
- Verificado: `assembleDebug` funde os 13 arquivos em `assets/` ✓.

## 6. Ciclo CDN executado (2026-11-10/11)

- **Publicado no HF** (`WorldRPG/worldrpg-game-files`, upload direcionado do delta): `cgui/` (69 arquivos), `fonts/` (+25 TTF, total 27), `data/script/mainV1.scm` (gap do manifesto corrigido).
- **Manifesto regenerado**: `manifest_android_mta.json` 264 → **358 arquivos**, 818.535.866 bytes (gerado 2026-11-11T00:10:36Z), publicado na raiz do dataset.
- **Oracle atualizado**: `file_server.py` (versão MTA-aware com `ANDROID_MTA_PATHS`), `manifest_android_mta.json` e `build_manifest_android_mta.py` sincronizados em `/home/ubuntu/world-rpg-br-en-eu/backend/server-launcher/`; container `worldrpg-file-server` reiniciado (Up).
- **Validação ponta a ponta (Regra 4 do skill cdn-asset-delivery-enforcer)**:
  - `GET :7785/manifest_android_mta.json` → 200, 358 arquivos ✓
  - `mainV1.scm`, `cgui/images/map_1024.png`, `fonts/unifont.ttf`, `texdb/gta3.img` via 7785 → 200 (307→HF) ✓
  - SHA-256 + tamanho conferidos byte a byte contra o manifesto nos 3 arquivos novos ✓
- Bug de roteamento confirmado e resolvido: o `file_server.py` antigo na VM prefixava tudo com `game_files/` (404 duplo-prefixo para MTA); a versão local já continha o pipeline correto (disco → `ANDROID_MTA_PATHS` → prefixos canônicos) e foi deployada.

## 7. Pendências restantes

- Corrigir/regenerar `texdb/menu/menu.pvr.dat` (§3.1) — requer regeneração do asset com o stream `mobilesc0` completo e nova revisão do manifesto.
- Validar no boot em dispositivo (P1A.4) quais ausências classe C realmente importam — esperado: nenhuma.
- Decidir empacotamento dos demais idiomas GXT (CDN, opcional).
- Nota: `game_files_android_mta/` NÃO está no espelho de disco da Oracle (780 MB) — servido 100% via redirect 307 para a CDN HF, mesmo padrão de `game_files/` e `game_files_android/` (excluídos do rsync do `start.sh deploy`).


## Validação no S25 em 2026-11-10

- Conferidos os 358 SHA-256 da geração instalada `2bf9fb2f6aac9f0ac62f2c87bf4702768e7ebb59`. Todos correspondem ao manifesto. Relatório em `outputs/device-assets-validation.json`.
- `EngineHostActivity` carrega o host Java/JNI de GTASA com Surface/EGL próprio a partir de Configurações → Testar GTA no APK debug. O menu gráfico renderizou no S25.
- Catálogo `assetfile.txt` regenerado com os 15 caminhos que existem no APK. O catálogo da distribuição original continha referências ausentes.
- `tools/repair_menu_texture.py` executado na Oracle recuperou o tamanho exato do stream de `mobilesc0`. Os seis pixels finais foram reconstruídos pela repetição do último pixel válido. Todos os pixels anteriores foram preservados e o resultado foi conferido por checksum e por um decoder com limites. O par DAT/TOC corrigido está no bootstrap do APK para validar a leitura, com o CDN preservado nesta rodada.
- Após o reparo, Start Game avançou além de `RLEDecompress`. O crash seguinte em `LoadObjectTypes` foi associado a `DATA/MAPS/LA/LAn.IDE`, enquanto o arquivo real é `LAn.ide`. A preparação agora cobre o stem misto e verifica os IDE/IPL declarados em `gta.dat` e `default.dat` antes de chamar a engine.
- A alegação anterior de que `data/script/mainV1.scm` faltava no manifesto inicial de 264 arquivos foi corrigida pela consulta à revisão fixa `a275d02a3c5d26e65889df8560d5481d4c766842`. A entrada já existia, com 3.169.739 bytes e SHA-256 `021ac87ff2f9eb6b76638529de67b55acfe102ab6b4f06226b8dbebcb1eaf62f`. A classificação histórica de gap não deve orientar novas correções.
