#!/usr/bin/env python3
"""Synthetic localization catalog/extraction checks; no game installation required."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import localization as l10n


class LocalizationTests(unittest.TestCase):
    def test_string_offsets_and_sentinel(self):
        self.assertEqual(l10n.strings_at(b"\x04\0\x08\0one\0two\0"), [b"one", b"two"])
        self.assertEqual(l10n.strings_at(b"\x04\0\x08\0one\0", allow_final_sentinel=True), [b"one", b""])
        for data in (b"", b"\xff\xff", b"\x02\0xx", b"\x04\0\xff\xffx\0"):
            with self.assertRaises(l10n.DataError): l10n.strings_at(data)

    def test_catalog_encoding_markers_and_duplicate_keys(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "cs.json"
            value = {"source": "Test %s @Yes@No", "source_sha256": "0" * 64,
                     "text": "Zkouška %s @Ano@Ne", "status": "reviewed"}
            obj = {"schema_version": 1, "language": "cs", "entries": {"ui:test": value}}
            path.write_text(json.dumps(obj), encoding="utf-8")
            l10n.compile_catalog([path], Path(tmp) / "catalog.inc")
            output = (Path(tmp) / "catalog.inc").read_text()
            self.assertIn(r"\232", output)
            self.assertNotIn("Zkouška", output)
            with self.assertRaises(l10n.DataError):
                l10n.compile_catalog([path, path], Path(tmp) / "duplicate.inc")
            for text in ("Test %d @Ano@Ne", "Test %s @Ano", "Test\0", "😀"):
                value["text"] = text
                path.write_text(json.dumps(obj), encoding="utf-8")
                with self.assertRaises((l10n.DataError, UnicodeError)):
                    l10n.read_catalog(path)

    def test_document_image_payloads_and_page_breaks(self):
        text = "Before @imDIALBTN:8 after @imABCDEFGH @pb %n %%"
        self.assertEqual(l10n.TOKENS.findall(text),
                         ["@imDIALBTN:8", "@imABCDEFGH", "@pb", "%n", "%%"])

    def test_duplicate_json_keys_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "cs.json"
            path.write_text('{"schema_version":1,"schema_version":1}')
            with self.assertRaises(l10n.DataError):
                l10n.read_catalog(path)

    def test_distributed_catalogs_have_no_original_game_corpus(self):
        for path in sorted(path for pattern in ("*-ui.json", "*-game.json")
                           for path in (ROOT / "localization").glob(pattern)):
            entries = l10n.read_catalog(path)
            for key, value in entries.items():
                if not key.startswith("ui:"):
                    self.assertNotIn("source", value)
                    if value["status"] == "preserve":
                        self.assertEqual(value["text"], "")

    def test_catalogs_are_scoped_by_language_and_encoding(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = []
            for language, text in (("cs", "Čeština"), ("es", "Español"), ("fr", "Français, cœur")):
                path = Path(tmp) / f"{language}.json"
                path.write_text(json.dumps({"schema_version": 1, "language": language,
                    "entries": {"ui:same": {"source_sha256": "0" * 64,
                    "text": text, "status": "reviewed"}}}), encoding="utf-8")
                paths.append(path)
            target = Path(tmp) / "catalog.inc"
            l10n.compile_catalog(paths, target)
            result = target.read_text()
            for language in ("cs", "es", "fr"):
                self.assertIn(f'"{language}:ui:same"', result)
            for glyph in (r"\310", r"\361", r"\347", r"\234"):
                self.assertIn(glyph, result)
            with self.assertRaises(l10n.DataError):
                l10n.compile_catalog(paths + paths[:1], target)

    def test_new_languages_cover_the_catalog_and_keep_original_fingerprints(self):
        names = l10n.read_catalog(ROOT / "localization/cs-game.json")
        interface = l10n.read_catalog(ROOT / "localization/cs-ui.json")
        for language in ("es", "fr"):
            for kind, original in (("game", names), ("ui", interface)):
                entries = l10n.read_catalog(ROOT / f"localization/{language}-{kind}.json")
                self.assertEqual(entries.keys(), original.keys())
                for key, value in entries.items():
                    self.assertEqual(value["source_sha256"], original[key]["source_sha256"])
                    if "source" in value:
                        self.assertEqual(l10n.TOKENS.findall(value["source"]),
                                         l10n.TOKENS.findall(value["text"]))
            self.assertFalse(any(v["status"] == "draft" for v in entries.values()))

    def test_new_name_buffers_and_counted_item_terminology(self):
        for language in ("es", "fr"):
            game = l10n.read_catalog(ROOT / f"localization/{language}-game.json")
            ui = l10n.read_catalog(ROOT / f"localization/{language}-ui.json")
            for key, value in game.items():
                if key.startswith("name:"):
                    self.assertLess(len(value["text"].encode("cp1252")), 54, key)
            for key, value in ui.items():
                if key.startswith("ui:plural.") and key.endswith(".0"):
                    self.assertEqual(game["name:" + key.split(".")[1]]["text"], value["text"])

    def test_drafts_do_not_count_as_complete(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "cs.json"
            value = {"source_sha256": "0" * 64, "text": "Zkouška", "status": "draft"}
            path.write_text(json.dumps({"schema_version": 1, "language": "cs",
                                        "entries": {"script:1:2:3": value}}))
            evidence = {"entries": {"script:1:2:3": {"source_sha256": "0" * 64, "tokens": []}}}
            self.assertFalse(l10n.audit([path], evidence)["complete"])
            value["status"] = "reviewed"
            path.write_text(json.dumps({"schema_version": 1, "language": "cs",
                                        "entries": {"script:1:2:3": value}}))
            self.assertTrue(l10n.audit([path], evidence)["complete"])

    def test_review_is_bound_to_source_translation_and_exception_reason(self):
        key = "script:1:2:3"
        value = {"source_sha256": "0" * 64, "text": "Zkouška", "status": "reviewed"}
        entries = {key: value}
        journal = {"schema_version": 1, "reviewer": "Synthetic fixture", "entries": {}}
        self.assertEqual(l10n.audit_review(entries, journal)["review_missing"], [key])
        decision = dict(value, translation_sha256=l10n.fingerprint(value["text"].encode("utf-8")),
                        reason="Compared with the fixture's original.")
        decision.pop("text")
        journal["entries"][key] = decision
        self.assertEqual(l10n.audit_review(entries, journal)["review_invalid"], [])
        for field, bad in (("translation_sha256", "f" * 64), ("source_sha256", "f" * 64),
                           ("status", "preserve"), ("reason", " ")):
            correct = decision[field]
            decision[field] = bad
            self.assertEqual(l10n.audit_review(entries, journal)["review_invalid"], [key])
            decision[field] = correct
        value.update(status="preserve", text="")
        decision.update(status="preserve", translation_sha256=l10n.fingerprint(b""),
                        reason="Technical identifier in the synthetic fixture.")
        self.assertEqual(l10n.audit_review(entries, journal)["review_invalid"], [])
        decision["source"] = "Original text must not be embedded in review decisions."
        self.assertEqual(l10n.audit_review(entries, journal)["review_invalid"], [key])

    def test_recorded_review_decisions_are_current(self):
        entries = l10n.read_catalog(ROOT / "localization/cs-game.json")
        journal = json.loads((ROOT / "localization/review.json").read_text(encoding="utf-8"))
        self.assertEqual(l10n.audit_review(entries, journal)["review_invalid"], [])

    def test_czech_name_capitalization_and_credits(self):
        entries = l10n.read_catalog(ROOT / "localization/cs-game.json")
        for index in (3, 33, 42, 437, 1029):
            self.assertTrue(entries[f"name:{index}"]["text"][0].islower())
        for index in (76, 545, 708, 722):
            self.assertTrue(entries[f"name:{index}"]["text"][0].isupper())
        for index in range(815, 889):
            self.assertTrue(entries[f"name:{index}"]["text"].startswith("svitek: "))
            self.assertTrue(entries[f"name:{index}"]["text"][8].islower())
        self.assertIn("Hlavní scenárista", entries["script:86:2:36"]["text"])
        self.assertIn("Matthew T. Kuzma", entries["script:86:2:36"]["text"])
        self.assertEqual(entries["script:86:2:160"]["status"], "preserve")
        ui = l10n.read_catalog(ROOT / "localization/cs-ui.json")
        self.assertFalse(any(v.get("source") == "Czech story text: draft." for v in ui.values()))


if __name__ == "__main__": unittest.main()
