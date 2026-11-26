"""Regenerate the configured MiSans LVGL fonts."""
import argparse
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
CONVERTER = Path("tools/font-converter/node_modules/lv_font_conv/lv_font_conv.js")


def static_characters(extra, directories=("main",)):
    characters = set(extra)
    for directory in directories:
        for source in (ROOT / directory).rglob("*"):
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
    parser.add_argument("--only", nargs="+", choices=[font["name"] for font in config["fonts"]],
                        help="Regenerate only the named fonts.")
    args = parser.parse_args()
    if not (ROOT / CONVERTER).is_file():
        parser.error("Install the locked converter with npm ci --prefix tools/font-converter.")
    font_dir = args.font_dir.resolve()
    fonts = [font for font in config["fonts"] if not args.only or font["name"] in args.only]
    for source in {font["source"] for font in fonts}:
        if not (font_dir / source).is_file():
            parser.error(f"Missing {source} in {font_dir}. See {config['source']}.")
    static = static_characters(config["static_symbols"])
    city = static | set(domestic_characters())
    for font in fonts:
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
            symbols = set(font.get("extra_symbols", ""))
            if "source_directories" in font:
                symbols.update(static_characters(config["static_symbols"], font["source_directories"]))
            elif coverage in ("static-cn", "dynamic-cn"):
                symbols.update(city if coverage == "dynamic-cn" else static)
            if symbols:
                command += ["--symbols", "".join(sorted(symbols))]
        subprocess.run(command, check=True, cwd=ROOT)
        if fallback := font.get("fallback"):
            path = ROOT / font["output"]
            generated = path.read_text(encoding="utf-8")
            generated = generated.replace("/*Initialize a public general font descriptor*/",
                                          f"LV_FONT_DECLARE({fallback});\n\n"
                                          "/*Initialize a public general font descriptor*/", 1)
            generated = generated.replace(".fallback = NULL,", f".fallback = &{fallback},", 1)
            path.write_text(generated, encoding="utf-8")


if __name__ == "__main__":
    main()
