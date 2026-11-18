"""Regenerate the configured MiSans LVGL fonts."""
import argparse
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
CONVERTER = Path("tools/font-converter/node_modules/lv_font_conv/lv_font_conv.js")


def static_characters(extra):
    characters = set(extra)
    for source in (ROOT / "main").rglob("*"):
        if source.suffix in (".cpp", ".h", ".hpp", ".cc"):
            characters.update(c for c in source.read_text(encoding="utf-8") if "\u4e00" <= c <= "\u9fff")
    return characters


def domestic_characters():
    for first in range(0xB0, 0xF8):
        for second in range(0xA1, 0xFF):
            value = bytes((first, second)).decode("gb2312", errors="ignore")
            if value:
                yield value


def main():
    config = json.loads((ROOT / "tools/fonts.json").read_text(encoding="utf-8"))
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font-dir", type=Path, default=ROOT / config["source_directory"])
    args = parser.parse_args()
    if not (ROOT / CONVERTER).is_file():
        parser.error("Install the locked converter with npm ci --prefix tools/font-converter.")
    font_dir = args.font_dir.resolve()
    for source in {font["source"] for font in config["fonts"]}:
        if not (font_dir / source).is_file():
            parser.error(f"Missing {source} in {font_dir}. See {config['source']}.")
    static = static_characters(config["static_symbols"])
    city = static | set(domestic_characters())
    for font in config["fonts"]:
        source = os.path.relpath(font_dir / font["source"], ROOT) if font_dir.is_relative_to(ROOT) else str(font_dir / font["source"])
        command = ["node", CONVERTER.as_posix(), "--size", str(font["size"]), "--bpp", str(font["bpp"]),
                   "--format", "lvgl", "--font", source, "-o", font["output"]]
        coverage = font["coverage"]
        if font["compression"] == "none":
            command += ["--no-compress"]
        if coverage == "symbols":
            command += ["--symbols", font["symbols"]]
        else:
            command += ["-r", "32-126"]
            if coverage in ("static-cn", "dynamic-cn"):
                command += ["--symbols", "".join(sorted(city if coverage == "dynamic-cn" else static))]
        subprocess.run(command, check=True, cwd=ROOT)


if __name__ == "__main__":
    main()
