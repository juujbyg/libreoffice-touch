#!/usr/bin/env python3
"""PC only: build installer/languages.json, the list of LibreOffice language packs the app can install.

For every language pack TDF publishes for this build, it downloads the pack, checks TDF's GPG
signature (the LibreOffice Build Team code signing key, by fingerprint) and TDF's published SHA-256,
and records the hash, size and names. The app then accepts a pack only if it matches this hash.

Usage: tools/langpacks.py <empty work folder>   (needs gpg and python3-babel; about 300 MB of downloads)
"""
import concurrent.futures
import hashlib
import html.parser
import io
import json
import os
import subprocess
import sys
import tarfile
import urllib.request

import babel

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "installer"))
import installer  # noqa: E402

BASE = installer.URL.rsplit("/", 1)[0] + "/"
PREFIX = "LibreOffice_%s_Linux_aarch64_deb_langpack_" % installer.BUILD
# LibreOffice Build Team (CODE SIGNING KEY) <build@documentfoundation.org>
TDF_KEY = "C2839ECAD9408FBE9531C3E9F434A1EFAFEEAEA3"
HEADERS = {"User-Agent": installer.USER_AGENT}
# qtz is LibreOffice's pseudo-language for translators (it shows message IDs), not a language.
SKIP = {"qtz"}
# Names CLDR gets wrong or ambiguous for LibreOffice's codes: (in the language itself, English).
NAMES = {"ca-valencia": ("Català (valencià)", "Catalan (Valencian)")}


def fetch(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=HEADERS), timeout=120) as r:
        return r.read()


class Links(html.parser.HTMLParser):
    def __init__(self):
        super().__init__()
        self.links = []

    def handle_starttag(self, tag, attrs):
        href = dict(attrs).get("href")
        if tag == "a" and href:
            self.links.append(href)


def codes():
    parser = Links()
    parser.feed(fetch(BASE).decode())
    return sorted({h[len(PREFIX):-len(".tar.gz")] for h in parser.links
                   if h.startswith(PREFIX) and h.endswith(".tar.gz")} - SKIP)


def names(code):
    """(name in the language itself, English name)."""
    if code in NAMES:
        return NAMES[code]
    try:
        locale = babel.Locale.parse(code.replace("-", "_"), sep="_")
        native, english = locale.get_display_name(locale), locale.get_display_name("en")
        native = native or english or code
        english = english or code
    except (ValueError, babel.UnknownLocaleError):
        try:
            locale = babel.Locale.parse(code.split("-")[0])
            native, english = locale.get_display_name(locale), locale.get_display_name("en")
            native, english = "%s (%s)" % (native, code), "%s (%s)" % (english, code)
        except (ValueError, babel.UnknownLocaleError):
            native = english = code
    return native[:1].upper() + native[1:], english


def check(code, work, gnupg):
    name = PREFIX + code + ".tar.gz"
    url = BASE + name
    pack, signature = fetch(url), fetch(url + ".asc")
    published = fetch(url + ".sha256").decode().split()[0]
    sha = hashlib.sha256(pack).hexdigest()
    if sha != published:
        raise SystemExit("%s: SHA-256 %s doesn't match TDF's %s" % (code, sha, published))
    path = os.path.join(work, name)
    with open(path, "wb") as f:
        f.write(pack)
    with open(path + ".asc", "wb") as f:
        f.write(signature)
    status = subprocess.run(["gpg", "--homedir", gnupg, "--status-fd", "1", "--verify", path + ".asc", path],
                            capture_output=True, text=True).stdout
    if "\n[GNUPG:] VALIDSIG %s " % TDF_KEY not in "\n" + status:
        raise SystemExit("%s: no valid signature from TDF's key\n%s" % (code, status))
    with tarfile.open(fileobj=io.BytesIO(pack)) as archive:
        debs = [m.name.rsplit("/", 1)[-1] for m in archive if m.name.endswith(".deb")]
    os.remove(path)
    native, english = names(code)
    return {"code": code, "name": native, "english": english, "size": len(pack), "sha256": sha,
            "dictionary": any("-dict-" in d for d in debs)}


def main():
    work = os.path.abspath(sys.argv[1])
    gnupg = os.path.join(work, "gnupg")
    os.makedirs(gnupg, mode=0o700, exist_ok=True)
    key = fetch("https://keyserver.ubuntu.com/pks/lookup?op=get&search=0x" + TDF_KEY)
    subprocess.run(["gpg", "--homedir", gnupg, "--import"], input=key, check=True, capture_output=True)
    found = codes()
    print("%d language packs" % len(found), flush=True)
    with concurrent.futures.ThreadPoolExecutor(4) as pool:
        languages = []
        for entry in pool.map(lambda c: check(c, work, gnupg), found):
            languages.append(entry)
            print("%-12s %-28s %6d KB  dictionary=%s" % (entry["code"], entry["english"], entry["size"] >> 10,
                                                         entry["dictionary"]), flush=True)
    languages.sort(key=lambda e: e["english"].lower())
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "installer", "languages.json")
    with open(out, "w", encoding="utf-8") as f:
        json.dump({"build": installer.BUILD, "signed_by": TDF_KEY, "languages": languages}, f,
                  ensure_ascii=False, indent=1)
        f.write("\n")
    print("wrote", os.path.normpath(out))


if __name__ == "__main__":
    main()
