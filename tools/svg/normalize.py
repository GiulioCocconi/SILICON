#!/usr/bin/env python3
"""Select SVG elements and assign Silicon palette colors in a local browser."""

import argparse
import io
import json
import re
import secrets
import shutil
import subprocess
import sys
import tempfile
import webbrowser
import xml.etree.ElementTree as ET
import zipfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit

SVG_NS = "http://www.w3.org/2000/svg"
SCRIPT_DIR = Path(__file__).resolve().parent
PALETTE_PATH = SCRIPT_DIR.parents[1] / "resources/app/palette.json"
DEFAULT_CONFIG = SCRIPT_DIR / "svgo.config.mjs"
MAX_SVG_BYTES = 10 * 1024 * 1024
MAX_REQUEST_BYTES = MAX_SVG_BYTES * 6 + 1024
ET.register_namespace("", SVG_NS)
ET.register_namespace("xlink", "http://www.w3.org/1999/xlink")


class SVGTreeBuilder(ET.TreeBuilder):
    def doctype(self, name, pubid, system):
        # This is called even for UTF-16/32 input, unlike a byte search.
        raise ValueError("DTD and entity declarations are forbidden")


def parse_svg(data: bytes) -> ET.Element:
    if len(data) > MAX_SVG_BYTES:
        raise ValueError("SVG exceeds the 10 MiB size limit")
    if b"<!DOCTYPE" in data.upper() or b"<!ENTITY" in data.upper():
        raise ValueError("DTD and entity declarations are forbidden")
    root = ET.fromstring(data, parser=ET.XMLParser(target=SVGTreeBuilder()))
    if root.tag != f"{{{SVG_NS}}}svg":
        raise ValueError("Not an SVG document")
    return root


def load_palette(path: Path = PALETTE_PATH) -> dict:
    palette = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(palette, dict) or set(palette) != {"light", "dark"}:
        raise ValueError("Palette must contain light and dark themes")
    for theme, colors in palette.items():
        if not isinstance(colors, dict) or not colors:
            raise ValueError(f"{theme}: expected a nonempty palette")
        for token, color in colors.items():
            if not re.fullmatch(r"SILICON_[A-Z][A-Z0-9_]*", token):
                raise ValueError(f"Invalid palette token: {token}")
            if not isinstance(color, str) or not re.fullmatch(r"#[0-9a-fA-F]{6}", color):
                raise ValueError(f"{theme}.{token}: expected #RRGGBB")
    if palette["light"].keys() != palette["dark"].keys():
        raise ValueError("Light and dark palette tokens must match")
    return palette


def sanitize_svg(root: ET.Element) -> bool:
    """Keep static SVG; the preview additionally blocks network loads with CSP."""
    changed = False
    forbidden = {"script", "foreignobject", "iframe", "object", "embed",
                 "animate", "animatemotion", "animatetransform", "set"}
    for parent in list(root.iter()):
        for child in list(parent):
            if child.tag.rsplit("}", 1)[-1].lower() in forbidden:
                parent.remove(child)
                changed = True
        for name, value in list(parent.attrib.items()):
            local = name.rsplit("}", 1)[-1].lower()
            unsafe_href = local == "href" and not (
                value.strip().startswith("#")
                or re.match(r"^data:image/(png|jpeg|gif|webp);base64,", value, re.I)
            )
            if local.startswith("on") or local == "base" or unsafe_href:
                del parent.attrib[name]
                changed = True
    return changed


def serialize(root: ET.Element) -> bytes:
    return ET.tostring(root, encoding="utf-8", xml_declaration=True)


def import_svg(data: bytes, config: Path = DEFAULT_CONFIG) -> dict:
    root = parse_svg(data)
    sanitized = sanitize_svg(root)
    root = optimize_svg(root, config)
    sanitized = sanitize_svg(root) or sanitized
    return {"svg": serialize(root).decode("utf-8"), "sanitized": sanitized}


def resolve_palette(root: ET.Element, colors: dict) -> None:
    """Resolve annotations with the same inline-style precedence as Silicon."""
    for element in root.iter():
        for prop in ("fill", "stroke"):
            color = colors.get(element.get(f"data-silicon-{prop}"))
            if color is None:
                continue
            style = element.get("style", "")
            pattern = re.compile(rf"(^|;)(\s*{prop}\s*:)[^;]*")
            if pattern.search(style):
                element.set("style", pattern.sub(
                    lambda m: m[1] + m[2] + color +
                    (" !important" if re.search(r"!\s*important\s*$", m[0]) else ""), style))
            else:
                element.set(prop, color)


def optimize(source: bytes, config: Path) -> bytes:
    if not shutil.which("svgo"):
        raise ValueError("SVGO is required to open SVGs. Install it with 'npm install -g svgo' "
                         "or run this tool in 'nix-shell -p svgo'.")
    if not config.is_file():
        raise ValueError(f"SVGO configuration not found: {config}")
    with tempfile.TemporaryDirectory(prefix="silicon-svg-") as directory:
        input_file = Path(directory) / "input.svg"
        output_file = Path(directory) / "output.svg"
        input_file.write_bytes(source)
        try:
            subprocess.run(
                ["svgo", "--config", str(config.resolve()), "--input", str(input_file),
                 "--output", str(output_file)],
                check=True, capture_output=True, text=True, timeout=60,
            )
        except subprocess.CalledProcessError as error:
            raise ValueError(f"SVGO failed: {(error.stderr or error.stdout).strip()}") from error
        except subprocess.TimeoutExpired as error:
            raise ValueError("SVGO timed out after 60 seconds; the SVG was not opened") from error
        return output_file.read_bytes()


def optimize_svg(root: ET.Element, config: Path) -> ET.Element:
    # A custom SVGO configuration must not silently lose or combine mappings.
    guard = "data-normalize-" + secrets.token_hex(12)
    expected = {}
    for element in root.iter():
        mappings = {p: element.get(f"data-silicon-{p}") for p in ("fill", "stroke")}
        if any(mappings.values()):
            key = str(len(expected))
            expected[key] = mappings
            element.set(guard, key)
    result = parse_svg(optimize(serialize(root), config))
    actual = {}
    for element in result.iter():
        key = element.attrib.pop(guard, None)
        mappings = {p: element.get(f"data-silicon-{p}") for p in ("fill", "stroke")}
        if key is not None:
            if key in actual:
                raise ValueError("SVGO duplicated a palette mapping; use the bundled configuration")
            actual[key] = mappings
        elif any(mappings.values()):
            raise ValueError("SVGO moved a palette mapping; use the bundled configuration")
    if actual != expected:
        raise ValueError("SVGO removed or changed palette mappings; use the bundled configuration")
    return result


def export_svg(source: bytes, palette: dict) -> bytes:
    """Serialize edits to an already optimized document without running SVGO."""
    root = parse_svg(source)
    sanitize_svg(root)
    resolve_palette(root, palette["light"])
    return serialize(root)


def process_file(source: Path, destination: Path, config: Path) -> None:
    """Noninteractive helper; writes only after successful optimization."""
    imported = import_svg(source.read_bytes(), config)
    destination.write_bytes(export_svg(imported["svg"].encode("utf-8"), load_palette()))


def export_name(source: str, used: set, overwrite: bool = False, reserved=None) -> str:
    """Match browser naming, keeping archive paths and duplicate names out."""
    stem = re.sub(r"\.svg$", "", source, flags=re.I)
    stem = re.sub(r'[<>:"/\\|?*\x00-\x1f]', "-", stem).rstrip(". ") or "document"
    base = stem if overwrite else f"{stem}.normalized"
    extension = re.search(r"\.svg$", source, flags=re.I) if overwrite else None
    extension = extension[0] if extension else ".svg"
    first = f"{base}{extension}"
    name, suffix = first, 2
    while name.lower() in used or (name != first and name.lower() in (reserved or set())):
        name = f"{base}-{suffix}{extension}"
        suffix += 1
    used.add(name.lower())
    return name


def export_archive(files: list, palette: dict, overwrite: bool = False) -> bytes:
    if not isinstance(files, list) or not files:
        raise ValueError("Expected a nonempty list of SVG files")
    if not isinstance(overwrite, bool):
        raise ValueError("Overwrite must be true or false")
    for item in files:
        if (not isinstance(item, dict) or not isinstance(item.get("name"), str)
                or not isinstance(item.get("svg"), str)):
            raise ValueError("Each file must contain a name and SVG string")
    archive = io.BytesIO()
    used = set()
    reserved = {export_name(item["name"], set(), overwrite=True).lower() for item in files}
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as output:
        for item in files:
            name = export_name(item["name"], used, overwrite, reserved)
            try:
                result = export_svg(item["svg"].encode("utf-8"), palette)
            except (ValueError, ET.ParseError, OSError) as error:
                raise ValueError(f'{item["name"]}: {error}') from error
            output.writestr(name, result)
    return archive.getvalue()


def make_server(sources: list[dict], palette: dict, config: Path, port: int = 0):
    token = secrets.token_urlsafe(32)
    assets = {"/": ("editor.html", "text/html; charset=utf-8"),
              "/editor.js": ("editor.js", "text/javascript; charset=utf-8"),
              "/editor.css": ("editor.css", "text/css; charset=utf-8")}

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, format, *args):
            pass  # Do not log the session token in the URL.

        def reply(self, status, body, content_type="application/json; charset=utf-8"):
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Referrer-Policy", "no-referrer")
            self.send_header("Content-Security-Policy", "default-src 'none'; script-src 'self'; "
                             "style-src 'self' 'unsafe-inline'; connect-src 'self'; "
                             "frame-src 'self'; img-src data:; base-uri 'none'; frame-ancestors 'none'")
            self.end_headers()
            self.wfile.write(body)

        def error(self, status, message):
            self.reply(status, json.dumps({"error": message}).encode())

        def authorized(self):
            origin = f"http://127.0.0.1:{self.server.server_port}"
            if self.headers.get("Host") != origin.removeprefix("http://"):
                self.error(403, "Invalid host")
                return False
            if self.headers.get("Origin", origin) != origin:
                self.error(403, "Invalid origin")
                return False
            if not secrets.compare_digest(self.headers.get("X-Normalize-Token", ""), token):
                self.error(403, "Invalid session; open the URL printed by normalize.py")
                return False
            return True

        def do_GET(self):
            path = urlsplit(self.path).path
            if path in assets:
                filename, content_type = assets[path]
                self.reply(200, (SCRIPT_DIR / filename).read_bytes(), content_type)
            elif path == "/api/state" and self.authorized():
                self.reply(200, json.dumps({"palette": palette, "files": sources,
                                           "svgo": bool(shutil.which("svgo"))}).encode())
            elif path != "/api/state":
                self.error(404, "Not found")

        def do_POST(self):
            if not self.authorized():
                return
            path = urlsplit(self.path).path
            if path not in {"/api/import", "/api/export", "/api/export-all"}:
                self.error(404, "Not found")
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if not 0 < length <= MAX_REQUEST_BYTES:
                    raise ValueError("Request exceeds the 60 MiB limit; save large documents individually")
                data = json.loads(self.rfile.read(length))
                if path == "/api/export-all":
                    if not isinstance(data, dict):
                        raise ValueError("Expected a list of SVG files")
                    self.reply(200, export_archive(data.get("files"), palette,
                                                  data.get("overwrite", False)), "application/zip")
                    return
                if not isinstance(data, dict) or not isinstance(data.get("svg"), str):
                    raise ValueError("Expected an SVG string")
                source = data["svg"].encode("utf-8")
                if path == "/api/import":
                    self.reply(200, json.dumps(import_svg(source, config)).encode())
                else:
                    self.reply(200, export_svg(source, palette), "image/svg+xml")
            except (ValueError, ET.ParseError, OSError) as error:
                self.error(400, str(error))

    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    server.daemon_threads = True
    return server, f"http://127.0.0.1:{server.server_port}/?token={token}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sources", type=Path, nargs="*", help="SVG files or directories to preload")
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG, help="SVGO configuration")
    parser.add_argument("--port", type=int, default=0, help="Local port (default: choose a free port)")
    parser.add_argument("--no-browser", action="store_true", help="Print the URL without opening it")
    args = parser.parse_args()
    try:
        palette = load_palette()
        if not args.config.is_file():
            raise ValueError(f"SVGO configuration not found: {args.config}")
        sources, seen = [], set()
        for path in args.sources:
            for source in sorted(path.rglob("*.svg")) if path.is_dir() else [path]:
                if source.resolve() in seen:
                    continue
                seen.add(source.resolve())
                try:
                    sources.append({"name": source.name, **import_svg(source.read_bytes(), args.config)})
                except (OSError, ValueError, ET.ParseError) as error:
                    print(f"Skipping {source}: {error}", file=sys.stderr)
        server, url = make_server(sources, palette, args.config, args.port)
    except (OSError, ValueError) as error:
        print(f"normalize.py: {error}", file=sys.stderr)
        return 1
    print(f"SVG palette editor: {url}", flush=True)
    print("Press Ctrl+C to stop. SVGs are optimized with SVGO when opened.", flush=True)
    if not shutil.which("svgo"):
        print("SVGO is missing: install it before opening SVGs (npm install -g svgo).", file=sys.stderr)
    if not args.no_browser:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
