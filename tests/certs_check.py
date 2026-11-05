"""PC test of start/certs.py: imports throwaway PKCS#12 files (made here with openssl) into a
temporary NSS database with the PC's own NSS, and checks listing and removal.
Run: python3 -I tests/certs_check.py"""
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "start"))
import certs  # noqa: E402


def sh(*args):
    subprocess.run(args, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def expect_error(text, job):
    try:
        job()
    except certs.CertError as e:
        assert text in str(e), "wrong message: %s" % e
        return
    raise AssertionError("no error, expected: " + text)


with tempfile.TemporaryDirectory() as work:
    w = lambda name: os.path.join(work, name)  # noqa: E731
    sh("openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", w("key.pem"), "-out", w("cert.pem"),
       "-days", "30", "-subj", "/CN=Test Signer/O=Touch test, Inc./emailAddress=test@example.invalid")
    sh("openssl", "pkcs12", "-export", "-in", w("cert.pem"), "-inkey", w("key.pem"), "-name", "Test Signer",
       "-out", w("test.p12"), "-passout", "pass:pässword 1")
    sh("openssl", "pkcs12", "-export", "-in", w("cert.pem"), "-inkey", w("key.pem"), "-out", w("empty.p12"),
       "-passout", "pass:")
    sh("openssl", "pkcs12", "-export", "-legacy", "-in", w("cert.pem"), "-inkey", w("key.pem"), "-name", "Legacy",
       "-out", w("legacy.p12"), "-passout", "pass:legacy")
    sh("openssl", "pkcs12", "-export", "-nokeys", "-in", w("cert.pem"), "-out", w("nokey.p12"), "-passout", "pass:x")
    with open(w("notes.p12"), "w") as f:
        f.write("not a certificate\n")
    fingerprint = subprocess.run(["openssl", "x509", "-in", w("cert.pem"), "-noout", "-fingerprint", "-sha256"],
                                 capture_output=True, text=True, check=True).stdout
    fingerprint = fingerprint.split("=", 1)[1].strip().replace(":", "").lower()

    data = w("data")
    assert certs.list_certificates(data) == []
    print("ok empty store")

    expect_error("Wrong password", lambda: certs.import_certificate(data, w("test.p12"), "nope"))
    print("ok wrong password refused")

    found = certs.import_certificate(data, w("test.p12"), "pässword 1")
    assert len(found) == 1, found
    c = found[0]
    assert c["new"] and c["name"] == "Test Signer", c
    assert c["organization"] == "Touch test, Inc.", c
    assert c["email"] == "test@example.invalid", c
    assert c["selfSigned"] and c["validUntil"] - c["validFrom"] > 29 * 86400, c
    assert c["id"] == fingerprint, (c["id"], fingerprint)
    assert oct(os.stat(os.path.join(data, "certificates")).st_mode & 0o777) == "0o700"
    print("ok imported, non-ASCII password, details and fingerprint")

    again = certs.import_certificate(data, w("test.p12"), "pässword 1")
    assert len(again) == 1 and not again[0]["new"], again
    print("ok importing twice keeps one")

    assert certs.remove_certificate(data, c["id"]) is True
    assert certs.list_certificates(data) == []
    assert certs.remove_certificate(data, c["id"]) is False
    print("ok removed, with its key")

    found = certs.import_certificate(data, w("empty.p12"), "")
    assert len(found) == 1 and found[0]["name"] == "Test Signer", found
    certs.remove_certificate(data, found[0]["id"])
    print("ok no name and empty password")

    try:
        found = certs.import_certificate(data, w("legacy.p12"), "legacy")
        print("ok legacy (3DES/RC2) file accepted")
        certs.remove_certificate(data, found[0]["id"])
    except certs.CertError as e:
        assert "old encryption" in str(e), e
        print("ok legacy file refused with a clear message")

    expect_error("no private key", lambda: certs.import_certificate(data, w("nokey.p12"), "x"))
    print("ok certificate without a key refused")
    leftovers = certs.list_certificates(data)
    assert leftovers == [], leftovers

    expect_error("isn't a certificate file", lambda: certs.import_certificate(data, w("notes.p12"), ""))
    print("ok not a certificate")
print("all certificate checks passed")
