# SVG palette editor

From the repository, enter the Nix development shell and launch the editor:

```sh
nix develop
python3 tools/svg/normalize.py
```

The default and clang development shells include Python and SVGO.
The editor itself uses only Python's standard library.
Outside Nix, use Python 3.10 or newer and install SVGO as described below.

The script starts a localhost server and opens your browser. Use **Open SVGs** to
select multiple files, or preload files/directories on the command line:

```sh
python3 tools/svg/normalize.py resources/app/gates resources/app/icons/save.svg
```

Each SVG runs through SVGO once when opened, including command-line inputs.
The preview and element list use the optimized result, so converted shapes are
available for selection immediately. A failed optimization leaves that file
unopened and reports the error; other files can still open. Original files on
disk are changed only when you save with overwrite enabled or choose to replace
one through Save As.

Select a shape in the preview or an element/group in the list. The inspector shows
its effective fill and stroke, original declarations, and palette mappings.
Choose a token in the dropdown or click a swatch. This works on ordinary SVGs:
assigning a color creates `data-silicon-fill` or `data-silicon-stroke` metadata.
**Original paint / restore original** restores that property's opened state;
**None** removes its paint and palette mapping. **Reset edits** restores the
optimized document as it was opened. Switching files retains edits.

The light/dark selector refreshes all mapped colors and the preview background
using `resources/app/palette.json`. Original unmapped paints stay unchanged;
`currentColor` inherits the preview theme's ink color unless the SVG defines it.
Theme changes do not mark files modified.

**Save As** and **Save All** serialize the edited document without running SVGO
again. They keep palette metadata and write light-theme fallback colors so
Silicon can resolve either theme at runtime. Install SVGO (tested with version 4)
before opening files:

```sh
npm install -g svgo
# Or, with Nix:
nix-shell -p svgo --run 'python3 tools/svg/normalize.py'
```

The default filename is `<name>.normalized.svg`. Chromium supports choosing the
destination with a native save picker; browsers without that API download the
optimized SVG instead. Original files are preserved unless you explicitly choose
to overwrite one. Cancelling or a failed save retains edits. Browsers can
warn about unsaved edits when closing or refreshing the page; keep the Python
process running until you finish saving. Stop it with Ctrl+C.

**Save All** exports every open document, including files without edits. In
Chromium, choose a destination folder once; the editor saves each
SVG there. Existing filenames and duplicate source names receive numbered
suffixes, so files are not overwritten. The status bar reports progress and any
failed files; successful files are marked saved, while failed files retain their
unsaved state. Edits made during saving remain unsaved until exported again.

Enable **Overwrite existing files** to use the original SVG filenames instead of
the `.normalized.svg` names. **Save All** then replaces matching files in the
chosen folder; choose the source folder to update the originals in place.
Duplicate filenames within the opened batch still receive numbered suffixes, so
each document is exported separately. **Save As** suggests the original filename
with this option enabled; the browser's save picker handles overwriting.
The option starts unchecked and is locked while a save is running.

Browsers without a folder picker download a single `silicon-normalized-svgs.zip`
archive containing all optimized SVGs, with unique filenames. With overwrite
enabled, archive entries use original filenames too; extracting or downloading
files follows the browser or archive tool's overwrite behavior. If any SVG cannot
be exported, the archive is not downloaded and every document retains its
unsaved state. Archive requests are limited to 60 MiB; larger collections can be
saved individually with **Save As**.

Additional options:

```sh
python3 tools/svg/normalize.py --no-browser --port 8765
python3 tools/svg/normalize.py --config /path/to/svgo.config.mjs input.svg
```

`--no-browser` prints the session URL. Open that exact URL, including its token.
The bundled SVGO configuration preserves element boundaries, IDs, references,
and the viewBox. Custom configurations that discard or combine palette mappings
are rejected. SVGO configurations are executable JavaScript; supply your own
trusted configuration.

The editor accepts static SVGs up to 10 MiB each. Import removes scripts, embedded
HTML, event handlers, animation, and external hrefs, and reports those removals.
Network resource loads are blocked in the sandboxed preview. SVG rendering uses
the browser; fonts and advanced effects can differ from Silicon's Qt renderer.

