# LibreOffice on the phone: phone-friendly layer

Installed on the phone; these are the sources.

- `lo` → `~/.local/bin/lo`. Sizing: `SAL_FORCEDPI=192` (LibreOffice's own drawing, `LO_DPI`) and
  `GDK_DPI_SCALE=1.8` (GTK text in menus and dialogs, `LO_TEXT_SCALE`). `GDK_SCALE` has no effect
  on Lomiri's Wayland. `GTK_THEME=LibreTouch`.
- `theme/LibreTouch` → `~/.local/share/themes/LibreTouch`: Adwaita plus finger-sized controls and
  the dialog-sheet look. Only LibreOffice uses it.
- `noparent.c` → `~/apps/libreoffice/gtk-im/lib/libnoparent.so` (LD_PRELOAD). Build:
  `sg docker -c "docker run --rm -v $PWD:/w -w /w clickable/amd64-ut24.04-1.x-arm64:latest aarch64-linux-gnu-gcc -O2 -Wall -shared -fPIC -o libnoparent.so noparent.c -ldl"`.
  `LO_SHIM_DEBUG=1` logs its decisions to `~/.cache/lo-last.log`.

Why the shim: Mir places a child window as if it were 640x480 (about 220 px from the left) and never
moves it, and a non-maximised top-level the same way. So dialogs get no parent and are maximised:
small ones are a card centred on an opaque grey backdrop (a translucent one shows the wallpaper,
because Lomiri hides the document window behind), large ones fill the screen, and ones bigger than
the screen scroll with their buttons kept visible. Document windows are always maximised, and
LibreOffice's minimum-size hints are capped at the screen size.

Profile defaults set (in `registrymodifications.xcu`): large toolbar and sidebar icons, Writer zoom
type 3 (page width). Backups on the phone: `~/.local/bin/lo.bak-20261104`,
`libnoparent-old.so`, `registrymodifications.xcu.bak-20261104`.

App identity (2026-11-04): the desktop entries run `lo` directly, inside Lomiri's own
`lomiri-app-launch--application-legacy--libreoffice-*` unit, so Lomiri shows the app's name and
icon. The old `lo-launch` escaped to a separate `systemd-run` unit because Lomiri stopped the app a
few seconds after launch; that was the splash screen closing before the main window opened, so `lo`
now passes `--nologo`. `lo-launch` only forwards to `lo`. Old entries: `~/.cache/desktop-bak-20261104/`.

Whole suite (2026-11-04), checked by screenshot after launching each app through Lomiri
(`tools/suite-screens.py`, run as root on the phone: `WAIT=<s> python3 suite-screens.py writer calc ...`):
- Writer, Calc: full screen, sidebar collapsed to its icon strip.
- Impress, Draw: the slide/page list is hidden (`MultiPaneGUI/SlideSorterBar/Visible`, View > Slide
  Pane brings it back) and the sidebar is collapsed (`Views/Windows/<app>/10336` UserData width 48),
  so the slide gets the width. Impress opens with its template picker as a full-screen sheet.
- Math: sidebar collapsed (Elements is one tap away), so the formula gets the full width.
- Base: its Database Wizard is a dialog LibreOffice draws itself in a fixed-size window, which GTK
  cannot scroll; its size follows the GTK text scale only, so Base runs at `GDK_DPI_SCALE=1.2`
  (wizard fits 1080 px with all buttons). Base starts slowly (~45 s to the wizard).
- The shim also lays out parentless windows built from .ui files like dialogs, and scrolls any
  oversized parentless window with its button row kept visible.
Profile tweaks are in `tools/lo-prefs*.py` (run with LibreOffice closed).

Opening files (2026-11-04): `tests/docs/Report.docx`, `Budget.xlsx` and `Slides.pptx` (made from the
`.html`, `.csv` and `.fodp` sources with LibreOffice on the PC) were opened on the phone through the
desktop entries with a `file://` URI, as tapping a file does, from a folder with a space in its name
(`tools/open-files.py`). All three opened correctly (formatting, table, accents and CJK; formulas
computed; both slides). Writer opened `.docx` files at the zoom saved in the file (100%, wider than
the screen), and ignoring the document's view settings (`Load/UserDefinedSettings`) didn't change
that, so `profile/PhoneView.xba` is installed in the profile's own Basic library and bound to the
Open Document event (`tools/lo-prefs5.py`): Writer documents open at page width, with no macro prompt.

## Publishing plan (not started)

Decided with the user on 2026-11-04, for when it is published:
- A small click that downloads the official LibreOffice for arm64 from The Document Foundation on
  first launch, checks it against a fixed hash and sets it up (as LibrePrint does), rather than
  bundling the 733 MB install.
- A neutral store name (e.g. "Office Suite", id `office.juujbyg`) with "runs the official
  LibreOffice" in the description, because "LibreOffice" is TDF's trademark.
- It has to be unconfined (files anywhere in home, its own libraries), so the OpenStore reviews it by
  hand. The user chose not to publish yet; nothing is pushed or uploaded.
