# Czech localization

**Work in progress, development alpha.** The runtime supports English and Czech. The Czech story catalog includes locally generated drafts and is not ready to be described as a fully reviewed translation. The catalog audit deliberately fails the completeness gate while drafts remain. See [recorded validation](../docs/validation.md#czech-localization-work-in-progress).

In Options, **Language...** opens a compact 487 × 200 dialog with English and Čeština radio choices. Apply changes the language immediately, including the still-open Options controls; Cancel discards that dialog's selection. Applying the active language preserves a later user choice to hide subtitles. The draft notice was removed at the user's request; review status remains documented here and enforced by the catalog audit. No restart is needed. `config.ini` stores `language=en|cs`, with English as the default. Switching to Czech enables subtitles; subsequently disabling subtitles remains a user preference across restarts. Voices retain the original English recordings. Missing voices retain readable text and choices. Already assembled speech retains its captured language, including after save/load; loading a save does not change the global language setting.

## Catalogs and local evidence

`cs-ui.json` contains native interface strings and counted-item forms. `cs-game.json` contains original name-index and script-table translations. Both are UTF-8, schema version 1, with `language: "cs"` and an `entries` object. Each entry has `source_sha256`, `text` and `status`. The source fingerprint hashes the exact original bytes, before decoding. Keys are:

| Key | Meaning |
|---|---|
| `ui:<sha256>` | Native interface literal, with its English `source` available for review |
| `ui:plural.<name-index>.<form>` | Counted-item forms: 0 for one, 1 for two through four, 2 otherwise |
| `name:<index>` | Original NAME resource index |
| `script:<segment>:<table-offset>:<index>` | Original interpreter segment, string-table byte offset and string index |

`reviewed` means the translation has been edited and checked. `draft` is embedded and usable but still needs linguistic review in context. `preserve` has an empty `text`; it instructs the runtime to retain the user's original string. This covers technical identifiers, unchanged proper names, author names and original licensing notices. Credits roles and headings are translated; author spelling and attribution order remain unchanged. Preserved originals are not copied into the source catalog. Native technical errors, resource identifiers and diagnostic output remain English.

Export the supported local installation's text and resource context only into `.cache/`:

```sh
python3 tools/localization.py export --data-dir original-game --output .cache/localization/source.json
python3 tools/localization.py audit --catalog localization/cs-ui.json --catalog localization/cs-game.json --evidence .cache/localization/source.json --review localization/review.json
```

The exporter validates the DOS input hashes and offsets. Its output includes original text, exact-byte fingerprints, formatting tokens, archive/resource identity and table indices. Keep this output private and outside source packages. The audit checks every exported candidate, source fingerprints, marker order and draft status; exit status 1 means incomplete. The initial checkpoint had 19,023 candidates: 9,835 preserved, 2,393 reviewed and 6,795 drafts. These are candidate strings, including technical strings, rather than 19,023 distinct dialogue lines. The native UI catalog is additional.

`review.json` records review decisions by stable key, exact source hash, UTF-8 translation hash, status and reason. It contains no original text corpus. Preserved entries need a specific exception reason. The audit uses this journal by default and rejects missing or stale decisions even if an entry says `reviewed`; changing status alone cannot satisfy the completion gate. There are 3,425 decisions: the preceding 2,568 draft reviews, 694 name-capitalization corrections and all 163 credits fragments. Capitalization corrections do not constitute a new semantic review of every name. There are still 4,227 draft entries and 15,598 candidates without a current journal decision.

CMake invokes the standard-library-only compiler automatically. Python 3.10+ is a build dependency; no translator, model or network connection is required for localization compilation or gameplay. The generated C++ array contains Windows-1250 translation bytes and hashes. A missing or stale key safely displays the original and emits a bounded English diagnostic. Catalog changes must retain argument order, `@` dialogue choices, `@im` image payloads and `@pb` page breaks. The audit requires local evidence because proprietary source text is absent from the game catalog.

## Terminology and layout

The brothers are **Julián, Filip and Kevin**, with inventory initials **J/F/K**. English retains Julian, Phillip and Kevin and J/P/K. Other proper names retain their spelling; descriptive object, creature, ability and spell names are translated. Counted mergeable items use explicit Czech one/few/many forms, including zero in the many form. Do not append English `s` to Czech labels. Rephrase object-name substitution templates when necessary to avoid attaching an incorrect Czech case ending to a name supplied in its base form.

Common object, creature, ability and role labels start with a lowercase letter, including spell names after `svitek:`. Proper names, personal epithets and possessive adjectives derived from proper names retain capitals, such as **Julián**, **Železný Jack** and **Pandořina skříňka**. Interface headings and sentence beginnings follow their own capitalization. Portrait names reuse the bold glyphs and shadow palette of the user's original name-plate artwork; the Czech accent and F are composed in memory. No extracted glyph assets are distributed.

Keep original font sizes, artwork dimensions and interaction regions. The runtime builds owned copies of the user's bitmap fonts and adds Czech accent glyphs without editing the original resource pack. Measure actual glyph widths and outlined ink bounds; shorten fixed-field wording instead of shrinking it. Documents retain their original page breaks, images, pagination and buffer/page limits. Speech captures the selected font and display text when assembled. Fixed-field, dialogue, placard and document checks record overflow and make the localization integration fail. Runtime measurement does not establish that every story branch has been exercised.

```sh
python3 tools/integration_localization.py --binary build/fta2-native --output .cache/localization-check
python3 tools/integration_localization.py --binary build/fta2-native --voice-file /path/to/FTAVOICE.HRS --output .cache/localization-check-voices
```

Use a fresh output directory for each run. The scenario covers both aspect ratios, both sidebars, both Options variants, Apply/Cancel, restart, captured speech saves, missing voices and real document scripts. Each process also verifies 96 radio clicks across circles and labels, repeated selection, E/C shortcuts, and active/queued status refresh with owned arguments. Exports, screenshots, saves and font-derived output stay in `.cache/`. A full linguistic review, all reachable story dialogue layouts, manual hardware checks, macOS and a complete playthrough remain outstanding.

## Draft translation provenance

Initial story drafts were generated locally using the Argos English–Czech 1.9.6 package and CTranslate2 4.6.0, then partially edited. No original game text was sent to an external translation service. The translator/model are development tools, stored outside the repository, and are not shipped or required at runtime.

The package README credits **Jörg Tiedemann and Santhosh Thottingal**, *OPUS-MT — Building open translation services for the World*, Proceedings of the 22nd Annual Conference of the European Association for Machine Translation, 2020, Lisbon, Portugal. It identifies the underlying OPUS model as [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). The pinned [package mirror](https://huggingface.co/TiberiuCristianLeon/Argostranslate/resolve/c57c8db597a5607fc23f21af7c9449d2cc7cc332/translate-en_cs-1_9_6.argosmodel) has SHA-256 `f2f5b93fc5a5aa3aa6bece99e0fa9f80487bb9a2ea4e0c3def58d6a4af37c81f` and declares `package_version=1.9.6`, `argos_version=1.9.0`, `from_code=en`, `to_code=cs`. Drafts and subsequent edits are identified by catalog status. Original game text and characters retain their original copyright; the engine's GPL does not relicense them.
