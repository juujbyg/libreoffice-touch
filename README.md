# LibreOffice for Ubuntu Touch (unofficial port)

The full LibreOffice suite (Writer, Calc, Impress, Draw, Math and Base) running natively on Ubuntu Touch phones, with an interface sized for fingers.

Package `libreoffice.juujbyg`, for arm64 phones on Ubuntu Touch 24.04.

This is an unofficial port, not made by The Document Foundation. "LibreOffice" and its logos are their
trademarks. The LibreOffice program is installed unmodified, exactly as The Document Foundation publishes it.

## How it works

- **LibreOffice isn't bundled.** The click is under 100 KB. On first launch, a screen offers to download
  the official LibreOffice 26.8.2 for Linux aarch64 from The Document Foundation's archive. The download is
  about 200 MB and takes about 730 MB once installed. It's checked against a fixed SHA-256 and unpacked
  into `~/.local/share/libreoffice.juujbyg/` (`installer/`). Nothing is installed system-wide and no root is
  needed. Deleting the app's data removes LibreOffice again.
- **Native on Lomiri.** LibreOffice runs with its GTK3 interface straight on Mir's Wayland. No Libertine
  container or X server is involved.
- **Phone-sized interface** (`bin/office`):
  - `SAL_FORCEDPI=192` and `GDK_DPI_SCALE=1.8` give large toolbars, menus and text. Base uses 1.2, because
    its Database Wizard is a fixed-size window that can't scroll.
  - The `LibreOfficePhone` GTK theme (`share/themes/`) makes buttons, menus, tabs, entries, scrollbars and the
    file picker finger-sized.
- **Dialogs as phone sheets** (`native/noparent.c`, loaded with `LD_PRELOAD`):
  - The problem: Mir places a dialog as if it were 640x480, about 220 px from the left, and never moves it.
  - The fix: every dialog is shown maximised. Small ones are a card centred on a grey backdrop, large ones
    fill the screen, and ones bigger than the screen scroll with their buttons kept visible.
  - Document windows are always maximised.
- **Phone defaults** (`profile/`, copied into a fresh profile on first run):
  - large icons;
  - Writer at page width, and documents opened at page width by a small macro that only sets the zoom;
  - the slide and page lists in Impress and Draw hidden;
  - sidebars folded to their icon strip.
- **On-screen keyboard:** Lomiri's keyboard works through the Maliit GTK input module (`lib/`, from
  Ubuntu's archive). Drag the keyboard down, or drag the page, to put it away; tap the page to bring it
  back (the shim handles both, since LibreOffice itself only asks for the keyboard when its window gains
  focus).
  - Lomiri only gives the keyboard to the focused app, and when one app has two windows it checks the
    first. So the start screen never runs LibreOffice inside its own app: it starts Writer, Calc and the
    others as their own apps through the URL dispatcher (`appid://`), leaving the document to open in
    `open-request`, or hands the document to a LibreOffice that's already running.
- **Languages** (`start/Languages.qml`, `installer/`): menus and spelling dictionaries for 122 languages,
  from TDF's language packs for the same build. `installer/languages.json` pins each pack's SHA-256;
  `tools/langpacks.py` made it after checking TDF's GPG signature on every pack. The first install also
  adds the phone's language. Packs can be added and removed on the Languages page, which also picks the
  menu language (passed to LibreOffice as `--language`). Removing a pack takes only the files it added.

The app is **confined**, like any normal store app, so LibreOffice only sees the app's own folders. Files
get in and out through Ubuntu Touch's Content Hub:
- The **LibreOffice** app (the start screen, `start/`) has buttons for new documents and lists the
  documents in `~/.local/share/libreoffice.juujbyg/Documents`. Tap a document to open it. Swipe it left to
  **Send** it (mail, messaging, printing), **Save to…** another app (such as the file manager), or delete it.
- **Open from…** at the top picks a document from another app. **Share** in another app (File Manager,
  Document Viewer, Downloads) can also send a file to LibreOffice. Either way, the file is copied into
  LibreOffice's folder and opened.
- LibreOffice's own Open and Save dialogs start in that folder.

The sandbox doesn't allow `/tmp`, dconf or programs from `/usr/bin`. So the launcher only uses shell
built-ins and a small bundled helper (`native/office-util.c`), and the shim moves LibreOffice's
single-instance socket into the app's runtime folder.

## Settings

Environment variables for `bin/office`:
- `LO_DPI`: LibreOffice's drawing size (default 192; 96 is desktop size).
- `LO_TEXT_SCALE`: text size in menus and dialogs (default 1.8).
- `LO_GTK_THEME`: the GTK theme.
- `LO_SHIM_DEBUG=1`: logs the dialog shim's decisions to `~/.cache/libreoffice.juujbyg/last.log`.
- `LO_SHIM_IM_TRACE=1`, or a file `~/.cache/libreoffice.juujbyg/im-trace`: logs every keyboard call there.

## Building

```sh
./build.sh            # needs Docker; writes build/libreoffice.juujbyg_<version>_arm64.click
python3 -I tests/installer_check.py   # installer logic with a fake archive, no network
python3 -I tests/hub_check.py         # start screen backend
```

`build.sh` cross-compiles the shim in the `clickable/amd64-ut24.04-1.x-arm64` image. It downloads the two
Maliit packages from Ubuntu's archive, checks them by SHA-256, and runs `click build`. `tools/` holds the
scripts used to test on a phone over SSH (screenshots of each app, first launch, opening files).

## Known limits

- Base starts slowly: about 45 s to its wizard on a Pixel 3a.
- Each app keeps LibreOffice's desktop menus and toolbars, made larger. This isn't a touch-first redesign.

## Licence

GPL v3 for this app's own files (see `LICENSE`). Third-party parts are listed in `NOTICE`.
