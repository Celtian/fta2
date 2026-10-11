#!/usr/bin/env python3
"""Exercise live widescreen switching with immutable local original game data."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

from fta2_data import sha256
from compare_reference import chunks

ROOT = Path(__file__).resolve().parents[1]


def validate_mental_owners(output):
    """Check the owning actor's complete diamond, across categories and selection changes."""
    result = json.loads((output / "mental-owner.json").read_text())
    assert result == {"status": "passed", "layouts": 4, "panels": 2, "brothers": 3,
                      "categories": 3, "redraw_checks": 288, "tooltip_checks": 744,
                      "edge_checks": 576, "passive_clicks": 72, "escape_closes": 24,
                      "save_load_checks": 24, "captures": 72, "fixture_restored": True}
    diamonds = {}
    for wide, width in ((0, 640), (1, 854)):
        for sidebar in range(2):
            for group in range(2):
                for brother in range(3):
                    for part in range(3):
                        name = f"mental-owner-{wide}-{sidebar}-{group}-{brother}-{part}.ppm"
                        image = (output / name).read_bytes()
                        header = f"P6\n{width} 480\n255\n".encode()
                        assert image.startswith(header) and len(image) == len(header) + width * 480 * 3
                        pixels = image[len(header):]
                        # The diamond overlays the head; compare its opaque pixels only.
                        diamond = tuple(pixels[((112 + y) * width + 106 + x) * 3:][:3]
                                        for y in range(21) for x in range(25)
                                        if abs(x - 12) * 10 + abs(y - 10) * 12 <= 120)
                        if brother in diamonds:
                            assert diamonds[brother] == diamond, name
                        else:
                            diamonds[brother] = diamond
                        glyph = tuple(pixels[((120 + y) * width + 110 + x) * 3:][:3]
                                      == bytes((196, 192, 180))
                                      for y in range(9) for x in range(13))
                        assert sum(glyph) == (10, 15, 14)[brother], name
    assert len(set(diamonds.values())) == 3
    return result


def validate_mental_scrolling(output):
    """Check actual mental categories, wheel routing and restored fixture state."""
    result = json.loads((output / "mental-scroll.json").read_text())
    assert result["status"] == "passed" and result["fixture_restored"]
    assert (result["layouts"], result["panels"], result["brothers"], result["categories"]) == (4, 2, 3, 3)
    assert result["captures"] == result["ability_interactions"] == 72
    assert result["category_checks"] == 216 and result["save_load_checks"] == 24
    assert result["placeholder_checks"] == 216 and result["redraw_checks"] == 504
    assert result["wheel_checks"] == 1680
    for wide, width in ((0, 640), (1, 854)):
        for sidebar in range(2):
            for group in range(2):
                for brother in range(3):
                    for part in range(3):
                        name = f"mental-scroll-{wide}-{sidebar}-{group}-{brother}-{part}.ppm"
                        image = (output / name).read_bytes()
                        header = f"P6\n{width} 480\n255\n".encode()
                        assert image.startswith(header) and len(image) == len(header) + width * 480 * 3
    return result


def validate_spell_ordering(output):
    """Check original spell identities, drag gestures and save persistence."""
    result = json.loads((output / "spell-order.json").read_text())
    assert result == {"status": "passed", "layouts": 4, "panels": 2, "brothers": 3,
                      "moves": 96, "swaps": 24, "cancels": 288, "wheel_checks": 168,
                      "redraw_checks": 264, "click_checks": 72, "save_load_checks": 24,
                      "captures": 24, "fixture_restored": True}
    for wide, width in ((0, 640), (1, 854)):
        for sidebar in range(2):
            for group in range(2):
                for brother in range(3):
                    image = (output / f"spell-order-{wide}-{sidebar}-{group}-{brother}.ppm").read_bytes()
                    header = f"P6\n{width} 480\n255\n".encode()
                    assert image.startswith(header) and len(image) == len(header) + width * 480 * 3
    return result


def validate_physical_ordering(output):
    """Check same-container swaps, original drop priorities and save persistence."""
    result = json.loads((output / "physical-order.json").read_text())
    assert result == {"status": "passed", "cases": 12, "swaps": 60,
                      "priority_checks": 72, "guard_checks": 60,
                      "redraw_checks": 60, "save_load_checks": 12,
                      "fixture_restored": True}
    for wide, width in ((0, 640), (1, 854)):
        for sidebar in range(2):
            for brother in range(3):
                image = (output / f"physical-order-{wide}-{sidebar}-{brother}.ppm").read_bytes()
                header = f"P6\n{width} 480\n255\n".encode()
                assert image.startswith(header) and len(image) == len(header) + width * 480 * 3
    return result


def validate_carried_storage(output):
    """Check automatic carried-container traversal, scripts and original UI dispatch."""
    result = json.loads((output / "carried-storage.json").read_text())
    assert result == {"status": "passed", "cases": 12, "placements": 168,
                      "guard_checks": 96, "script_checks": 48,
                      "ui_transfer_checks": 36, "redraw_checks": 24,
                      "save_load_checks": 12, "fixture_restored": True}
    for wide, width in ((0, 640), (1, 854)):
        for sidebar in range(2):
            for brother in range(3):
                image = (output / f"carried-storage-{wide}-{sidebar}-{brother}.ppm").read_bytes()
                header = f"P6\n{width} 480\n255\n".encode()
                assert image.startswith(header) and len(image) == len(header) + width * 480 * 3
    return result


def validate_inventory_ui(output):
    """Check physical wheel/placeholder and game-clock integration evidence."""
    result = json.loads((output / "inventory-ui.json").read_text())
    assert result == {"status": "passed", "layouts": 4, "decorations": 4,
                      "wheel_checks": 384, "placeholder_checks": 48,
                      "redraw_checks": 80, "options_checks": 8, "clock_checks": 12,
                      "save_load_checks": 4, "fixture_restored": True}
    for wide, width in ((0, 640), (1, 854)):
        for sidebar in range(2):
            for kind, count in (("inventory", 4), ("clock", 3), ("clock-options", 2), ("hotkeys", 2)):
                for sample in range(count):
                    name = f"{kind}-{wide}-{sidebar}-{sample}.ppm"
                    image = (output / name).read_bytes()
                    header = f"P6\n{width} 480\n255\n".encode()
                    assert image.startswith(header)
                    assert len(image) == len(header) + width * 480 * 3, name
    return result


def validate_container_owners(output):
    """Validate real container ownership, redraws and original-palette badge captures."""
    result = json.loads((output / "container-owner.json").read_text())
    assert result == {"status": "passed", "layouts": 4, "decorations": 4, "brothers": 3,
                      "redraw_checks": 176, "tooltip_checks": 552, "edge_checks": 256,
                      "transfers": 280, "neutral_checks": 32, "escape_closes": 16,
                      "save_load_checks": 4, "captures": 48, "fixture_restored": True}
    colors = {}
    initials = {}
    for wide, width in ((0, 640), (1, 854)):
        for sidebar in range(2):
            for decoration in range(4):
                for brother in range(3):
                    image = (output / f"owner-{wide}-{sidebar}-{decoration}-{brother}.ppm").read_bytes()
                    header = f"P6\n{width} 480\n255\n".encode()
                    assert image.startswith(header) and len(image) == len(header) + width * 480 * 3
                    # Captured after transferring the outer bag to the next brother.
                    carrier = (brother + 1) % 3
                    # Sample the left facet, clear of the cursor's opaque hotspot.
                    x, y = 30 + decoration * 8 + 125, 70 + 76
                    pixel = image[len(header) + (y * width + x) * 3:][:3]
                    if carrier in colors:
                        assert pixel == colors[carrier], (wide, sidebar, decoration, brother)
                    else:
                        colors[carrier] = pixel
                    # Original ThinFix8 initials must remain visible and identical across layouts.
                    glyph = tuple(image[len(header) + (row * width + 30 + decoration * 8 + col) * 3:][:3]
                                  == bytes((196, 192, 180))
                                  for row in range(144, 153) for col in range(125, 138))
                    # Keep every original-font pixel, including beneath oversized chest sprites.
                    assert sum(glyph) == (10, 15, 14)[carrier], (wide, sidebar, decoration, brother)
                    if carrier in initials:
                        assert glyph == initials[carrier], (wide, sidebar, decoration, brother)
                    else:
                        initials[carrier] = glyph
    red, blue, yellow = (colors[i] for i in range(3))
    assert red[0] > red[1] and red[0] > red[2]
    assert blue[2] > blue[0] and blue[2] > blue[1]
    assert yellow[0] > yellow[2] and yellow[1] > yellow[2]
    assert len(set(initials.values())) == 3
    validate_mental_owners(output)
    validate_inventory_ui(output)
    validate_mental_scrolling(output)
    validate_spell_ordering(output)
    validate_physical_ordering(output)
    validate_carried_storage(output)
    for wide in range(2):
        for sidebar in range(2):
            for pregame in range(2):
                audio = json.loads((output / f"options-audio-{wide}-{sidebar}-{pregame}.json").read_text())
                assert audio["status"] == "passed" and audio["preference_saved"]
                assert audio["night_bounds"] == [14, 206, 18, 17]
                low, middle, high = audio["speech_levels"]
                assert low == 0 and 0 < middle < high == 255
    return result


def validate_exploration(output, saves):
    """Check isolated exploration evidence, including actual rendered map interiors."""
    exploration = json.loads((output / "exploration.json").read_text())
    assert exploration["status"] == "passed"
    assert exploration["footprints_compared"] == 16
    assert exploration["brother_maps_checked"] == 14
    assert exploration["world_switches_checked"] == 2
    assert exploration["shared_world_exploration"] and exploration["fixture_restored"]
    assert exploration["radius_metatiles"] == 2 and exploration["corners_excluded"]
    assert len(exploration["samples"]) == 30
    for mode in ("classic", "wide"):
        for brother in range(3):
            samples = [s for s in exploration["samples"]
                       if s["mode"] == mode and s["brother"] == brother]
            assert [s["step"] for s in samples] == list(range(5))
            assert samples[-1]["u"] - samples[0]["u"] == 128
            assert samples[-1]["v"] - samples[0]["v"] == 128
            if mode == "wide":
                assert all(s["side_pixels"] == 214 * 420 for s in samples)
    before_fixture, restored_fixture = chunks(saves / "fta2.003"), chunks(saves / "fta2.004")
    for tag in (b"AMAP", b"NRNG"):
        assert before_fixture[tag] == restored_fixture[tag], tag
    maps = {}
    for mode, width, origin_x in (("classic", 640, 52), ("wide", 854, 155)):
        for brother in range(3):
            for step in ((0, 4, 5) if brother == 2 else (0, 4)):
                image = (output / f"exploration-{mode}-{brother}-{step}.ppm").read_bytes()
                header = f"P6\n{width} 480\n255\n".encode()
                assert image.startswith(header) and len(image) == len(header) + width * 480 * 3
                pixels = image[len(header):]
                # Compare the opaque 524 x 298 map interior, excluding its shadow.
                # Classic retains the original (52,54) dialog position; wide centers at (155,54).
                crop = b"".join(pixels[(y * width + origin_x + 10) * 3:
                                       (y * width + origin_x + 534) * 3]
                                for y in range(64, 362))
                if mode == "classic":
                    maps[brother, step] = crop
                else:
                    assert crop == maps[brother, step], (brother, step)
    return dict(exploration, map_interior_parity=True, map_interiors_compared=7)


def validate_buttons(output):
    """Validate immediate redraw evidence and both Center On image states."""
    result = json.loads((output / "buttons.json").read_text())
    assert result == {"status": "passed", "immediate_redraw_checks": 200,
                      "selections": 48, "aggression_banding_toggles": 96,
                      "portrait_changes": 48, "layouts": 4, "repetitions": 2,
                      "captures": 24, "fixture_restored": True}
    center_images = {}
    for mode, width, shift in (("classic", 640, 0), ("wide", 854, 214)):
        for panel in ("group", "individual"):
            for method in ("mouse", "key"):
                for brother in range(3):
                    name = f"buttons-{mode}-{panel}-{method}-{brother}.ppm"
                    image = (output / name).read_bytes()
                    header = f"P6\n{width} 480\n255\n".encode()
                    assert image.startswith(header) and len(image) == len(header) + width * 480 * 3
                    pixels = image[len(header):]
                    for slot in range(3 if panel == "group" else 1):
                        # The compressed image has transparent corners over different panel backgrounds.
                        x, y = 561 + shift, 58 + 150 * slot
                        crop = b"".join(pixels[(row * width + x) * 3:
                                               (row * width + x + 24) * 3]
                                        for row in range(y, y + 23))
                        selected = panel == "individual" or slot == brother
                        if selected in center_images:
                            assert crop == center_images[selected], name
                        else:
                            center_images[selected] = crop
    assert center_images[True] != center_images[False]
    return dict(result, center_images_match_across_modes_and_input=True)


def validate_sidebar(output, saves, initial):
    """Check the optional layout, nested gold fixture and checkpoint restoration."""
    assert json.loads((output / "sidebar-startup.json").read_text()) == {
        "alternate_sidebar": initial}
    result = json.loads((output / "sidebar-layout.json").read_text())
    assert result == {"status": "passed", "mental_clicks": 102, "group_mental_clicks": 54,
                      "mental_redraw_checks": 102, "gold_checks": 14,
                      "mental_pressed_checks": 90,
                      "mental_tooltip_checks": 90,
                      "mental_default_positions": 84, "mental_preserved_positions": 18,
                      "mental_saved_positions": 12, "carry_footer_checks": 14,
                      "container_hover_closes": 84,
                      "container_item_hover_closes": 54,
                      "grid_cells": 72, "grid_edge_checks": 288,
                      "grid_tooltip_checks": 288, "hp_redraw_checks": 60,
                      "grid_button_redraw_checks": 144,
                      "gold_tooltip_checks": 86, "gold_inactive_checks": 10,
                      "gold_passive_clicks": 2,
                      "options_openings": 13, "fixture_restored": True,
                      "preference_enabled": True}
    assert "alternate_sidebar=true" in (saves / "config.ini").read_text()
    before, after = chunks(saves / "fta2.006"), chunks(saves / "fta2.008")
    # The original loader advances clock/fade timestamps and refreshes sight/animation caches.
    # Protect all other chunks and every object byte apart from the documented sight counter.
    transient = {b"TIME", b"PALE", b"ACTR", b"OBJS"}
    assert before.keys() == after.keys()
    for tag in before.keys() - transient:
        assert before[tag] == after[tag], tag
    objects_before, objects_after = bytearray(before[b"OBJS"]), bytearray(after[b"OBJS"])
    assert len(objects_before) == len(objects_after) and (len(objects_before) - 6) % 32 == 0
    for offset in range(6 + 29, len(objects_before), 32):
        objects_before[offset] = objects_after[offset] = 0
    assert objects_before == objects_after, "Sidebar fixture altered inventory object records"
    panels = {}
    for mode, width, shift in (("classic", 640, 0), ("wide", 854, 214)):
        for brother in range(3):
            image = (output / f"sidebar-{mode}-{brother}.ppm").read_bytes()
            header = f"P6\n{width} 480\n255\n".encode()
            assert image.startswith(header) and len(image) == len(header) + width * 480 * 3
            tooltip_image = (output / f"gold-tooltip-{mode}-{brother}.ppm").read_bytes()
            assert tooltip_image.startswith(header) and len(tooltip_image) == len(image)
            assert tooltip_image != image, "Gold tooltip capture did not include the cursor"
            pixels = image[len(header):]
            panel = b"".join(pixels[(y * width + 460 + shift) * 3:
                                     (y * width + 640 + shift) * 3]
                             for y in range(20, 440))
            if mode == "classic":
                panels[brother] = panel
            else:
                assert panels[brother] == panel, (mode, brother)
        for brother in range(3):
            image = (output / f"sidebar-group-{mode}-{brother}.ppm").read_bytes()
            header = f"P6\n{width} 480\n255\n".encode()
            assert image.startswith(header)
            pixels = image[len(header):]
            panel = b"".join(pixels[(y * width + 460 + shift) * 3:
                                     (y * width + 640 + shift) * 3]
                             for y in range(20, 440))
            if mode == "classic":
                panels["group", brother] = panel
            else:
                assert panels["group", brother] == panel, (mode, brother)
        for kind in ("group", "individual"):
            for brother in range(3):
                image = (output / f"mental-position-{mode}-{kind}-{brother}.ppm").read_bytes()
                header = f"P6\n{width} 480\n255\n".encode()
                assert image.startswith(header) and len(image) == len(header) + width * 480 * 3
                pixels = image[len(header):]
                window = b"".join(pixels[(y * width + 478 + shift) * 3:
                                          (y * width + 636 + shift) * 3]
                                  for y in range(114, 329))
                if mode == "classic":
                    panels["mental", kind, brother] = window
                else:
                    assert panels["mental", kind, brother] == window, (mode, kind, brother)
        for kind in ("group", "individual"):
            for brother in range(3):
                captures = [(output / f"tooltip-{when}-close-{mode}-{kind}-{brother}.ppm").read_bytes()
                            for when in ("before", "after")]
                header = f"P6\n{width} 480\n255\n".encode()
                assert all(image.startswith(header) and len(image) == len(header) + width * 480 * 3
                           for image in captures)
                assert captures[0] != captures[1], "Escape did not change the tooltip/window capture"
        assert (output / f"sidebar-zero-{mode}.ppm").exists()
        for menu in ("game", "pregame"):
            for state in ("on", "off"):
                image = (output / f"sidebar-options-{mode}-{menu}-{state}.ppm").read_bytes()
                assert image.startswith(f"P6\n{width} 480\n255\n".encode())
    return dict(result, persisted_startup_checked=True, checkpoint_chunks_identical=len(before.keys() - transient),
                inventory_objects_restored=True,
                mental_window_images_match_across_modes=True,
                sidebar_images_match_across_modes=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--data-dir", type=Path, default=ROOT / "original-game")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    binary, game, work = args.binary.resolve(), args.data_dir.resolve(), args.output.resolve()
    if work == game or game in work.parents:
        raise RuntimeError("Test output must be outside originals")
    before = {str(p.relative_to(game)): sha256(p.read_bytes())
              for p in game.rglob("*") if p.is_file()}
    work.mkdir(parents=True, exist_ok=False)
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    results = []
    for index in range(2):
        saves, output = work / f"saves-{index}", work / f"output-{index}"
        saves.mkdir()
        output.mkdir()
        shutil.copyfile(game / "999.SAV", saves / "fta2.002")
        if index:
            shutil.copyfile(work / "saves-0/config.ini", saves / "config.ini")
        with (output / "native.log").open("w") as log:
            subprocess.run([str(binary), "--data-dir", str(game), "--save-dir", str(saves),
                            "--smoke-test", str(output), "--smoke-widescreen"],
                           env=env, check=True, timeout=300, stdout=log, stderr=subprocess.STDOUT)
        startup = json.loads((output / "startup.json").read_text())
        assert startup == {"widescreen": bool(index), "width": 854 if index else 640}
        assert json.loads((output / "clock-startup.json").read_text()) == {"show_game_time": bool(index)}
        assert "show_game_time=true" in (saves / "config.ini").read_text()
        result = json.loads((output / "widescreen.json").read_text())
        assert result["status"] == "passed"
        assert result["checkbox_redraw_parity"] and result["checkbox_edge_clicks"]
        assert result["options_layouts_checked"] == 4
        exploration = validate_exploration(output, saves)
        buttons = validate_buttons(output)
        sidebar = validate_sidebar(output, saves, bool(index))
        owners = validate_container_owners(output)
        assert "widescreen=true" in (saves / "config.ini").read_text()
        for name in ("wide-group.ppm", "wide-individual.ppm", "wide-final.ppm"):
            image = (output / name).read_bytes()
            header = b"P6\n854 480\n255\n"
            assert image.startswith(header) and len(image) == len(header) + 854 * 480 * 3
        for mode, width in (("classic", 640), ("wide", 854)):
            for menu in ("game", "pregame"):
                # The selected checkbox now always agrees with the live framebuffer.
                state = "checked" if mode == "wide" else "unchecked"
                image = (output / f"options-{mode}-{menu}-{state}.ppm").read_bytes()
                header = f"P6\n{width} 480\n255\n".encode()
                assert image.startswith(header) and len(image) == len(header) + width * 480 * 3
        results.append({"startup": startup, "checks": result, "exploration": exploration, "buttons": buttons, "sidebar": sidebar, "container_owners": owners})
    after = {str(p.relative_to(game)): sha256(p.read_bytes())
             for p in game.rglob("*") if p.is_file()}
    assert before == after
    report = {"status": "passed", "original_files_unchanged": len(before), "runs": results,
              "backends": {"video": "dummy", "audio": "dummy"}}
    (work / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
