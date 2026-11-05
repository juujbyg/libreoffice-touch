"""Signing certificates for LibreOffice: an NSS certificate database in the app's data folder.

LibreOffice signs documents (File > Digital Signatures, and signed PDF export) with X.509 certificates
from an NSS database. On a desktop it finds Firefox's or Thunderbird's; in the app sandbox there is
none, so the app keeps its own in <data>/certificates, and bin/office points LibreOffice at it.

Certificates come as PKCS#12 files (.p12 or .pfx, with the private key, protected by a password).
They're imported with the phone's own NSS (the library LibreOffice signs with), called through ctypes
because the sandbox can't run NSS's command-line tools. The database itself has no password of its
own: like a browser without a primary password, it relies on the app's private folder.
"""
import ctypes
import ctypes.util
import hashlib
import os
import threading

FOLDER = "certificates"
EXTENSIONS = (".p12", ".pfx")
# PK11CertListType: certificates that have a private key in the database, once each.
PK11_CERT_LIST_USER_UNIQUE = 5
SEC_SUCCESS = 0

_lock = threading.Lock()  # NSS start-up and shut-down are per process
_nss = None


class CertError(Exception):
    pass


class SECItem(ctypes.Structure):
    _fields_ = [("type", ctypes.c_int), ("data", ctypes.c_void_p), ("len", ctypes.c_uint)]


class PRCList(ctypes.Structure):
    pass


PRCList._fields_ = [("next", ctypes.POINTER(PRCList)), ("prev", ctypes.POINTER(PRCList))]


class CERTCertListNode(ctypes.Structure):
    _fields_ = [("links", PRCList), ("cert", ctypes.c_void_p), ("appData", ctypes.c_void_p)]


class CERTCertList(ctypes.Structure):
    _fields_ = [("list", PRCList), ("arena", ctypes.c_void_p)]


# The start of NSS's CERTCertificate (certt.h), which has been stable since NSS 3.0:
# arena, subjectName, issuerName, signatureWrap (CERTSignedData: 2 SECItems and an algorithm ID of
# 2 more), then derCert.
class CERTCertificateHead(ctypes.Structure):
    _fields_ = [("arena", ctypes.c_void_p), ("subjectName", ctypes.c_char_p), ("issuerName", ctypes.c_char_p),
                ("signatureWrap", SECItem * 4), ("derCert", SECItem)]


NICKNAME_CALLBACK = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.POINTER(SECItem), ctypes.POINTER(ctypes.c_int),
                                     ctypes.c_void_p)


def _load(name):
    # By soname first: find_library runs ldconfig, which the sandbox doesn't allow.
    for candidate in ("lib%s.so" % name, None):
        candidate = candidate or ctypes.util.find_library(name)
        if candidate:
            try:
                return ctypes.CDLL(candidate)
            except OSError:
                pass
    raise CertError("The phone's security library (NSS) is missing.")


def _libs():
    global _nss
    if _nss:
        return _nss
    nss, smime, nspr = _load("nss3"), _load("smime3"), _load("nspr4")
    p, i, u = ctypes.c_void_p, ctypes.c_int, ctypes.c_uint
    sigs = [
        (nss, "NSS_InitContext", p, [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, p, u]),
        (nss, "NSS_ShutdownContext", i, [p]),
        (nss, "PK11_GetInternalKeySlot", p, []),
        (nss, "PK11_FreeSlot", None, [p]),
        (nss, "PK11_NeedUserInit", i, [p]),
        (nss, "PK11_InitPin", i, [p, ctypes.c_char_p, ctypes.c_char_p]),
        (nss, "PK11_ListCerts", ctypes.POINTER(CERTCertList), [i, p]),
        (nss, "CERT_DestroyCertList", None, [ctypes.POINTER(CERTCertList)]),
        (nss, "CERT_GetCertTimes", i, [p, ctypes.POINTER(ctypes.c_int64), ctypes.POINTER(ctypes.c_int64)]),
        (nss, "CERT_MakeCANickname", p, [p]),
        (nss, "PK11_DeleteTokenCertAndKey", i, [p, p]),
        (smime, "SEC_PKCS12DecoderStart", p, [ctypes.POINTER(SECItem), p, p, p, p, p, p, p]),
        (smime, "SEC_PKCS12DecoderUpdate", i, [p, ctypes.c_char_p, ctypes.c_ulong]),
        (smime, "SEC_PKCS12DecoderVerify", i, [p]),
        (smime, "SEC_PKCS12DecoderValidateBags", i, [p, NICKNAME_CALLBACK]),
        (smime, "SEC_PKCS12DecoderImportBags", i, [p]),
        (smime, "SEC_PKCS12DecoderFinish", None, [p]),
        (nss, "SECITEM_AllocItem", ctypes.POINTER(SECItem), [p, p, u]),
        (nss, "PORT_GetError", i, []),
        (nspr, "PR_ErrorToName", ctypes.c_char_p, [i]),
    ]
    for lib, name, restype, argtypes in sigs:
        f = getattr(lib, name)
        f.restype, f.argtypes = restype, argtypes
    _nss = {name: getattr(lib, name) for lib, name, _r, _a in sigs}
    return _nss


def _error_name():
    n = _libs()
    name = n["PR_ErrorToName"](n["PORT_GetError"]())
    return name.decode() if name else "unknown"


class _Database:
    """The app's certificate database, open for one operation (made on first use)."""

    def __init__(self, data):
        self.folder = os.path.join(data, FOLDER)

    def __enter__(self):
        _lock.acquire()
        try:
            n = _libs()
            os.makedirs(self.folder, mode=0o700, exist_ok=True)
            os.chmod(self.folder, 0o700)
            self.ctx = n["NSS_InitContext"](("sql:" + self.folder).encode(), b"", b"", b"", None, 0)
            if not self.ctx:
                raise CertError("Couldn't open the certificate store (%s)." % _error_name())
            self.slot = n["PK11_GetInternalKeySlot"]()
            if n["PK11_NeedUserInit"](self.slot):
                # A new database: set it up with an empty password (see the module's note).
                n["PK11_InitPin"](self.slot, None, b"")
            return self
        except BaseException:
            self._close()
            raise

    def _close(self):
        n = _libs()
        if getattr(self, "slot", None):
            n["PK11_FreeSlot"](self.slot)
            self.slot = None
        if getattr(self, "ctx", None):
            n["NSS_ShutdownContext"](self.ctx)
            self.ctx = None
        _lock.release()

    def __exit__(self, *exc):
        self._close()

    def certificates(self):
        """[(info, cert pointer)] for every certificate with a private key."""
        n = _libs()
        found = []
        listing = n["PK11_ListCerts"](PK11_CERT_LIST_USER_UNIQUE, None)
        if not listing:
            return found, None
        head = ctypes.addressof(listing.contents.list)
        node = listing.contents.list.next
        while node and ctypes.addressof(node.contents) != head:
            cert = ctypes.cast(node, ctypes.POINTER(CERTCertListNode)).contents.cert
            found.append((_describe(cert), cert))
            node = node.contents.next
        return found, listing


def _rdn(name, key):
    """One attribute (CN, O, E...) out of NSS's RFC 1485 name string."""
    parts, current, escaped, quoted = [], "", False, False
    for ch in name:
        if escaped:
            current += ch
            escaped = False
        elif ch == "\\":
            escaped = True
        elif ch == '"':
            quoted = not quoted
        elif ch in ",+" and not quoted:
            parts.append(current)
            current = ""
        else:
            current += ch
    parts.append(current)
    for part in parts:
        k, _, v = part.strip().partition("=")
        if k.strip().upper() == key:
            return v.strip()
    return ""


def _describe(cert):
    n = _libs()
    head = ctypes.cast(cert, ctypes.POINTER(CERTCertificateHead)).contents
    subject = (head.subjectName or b"").decode("utf-8", "replace")
    issuer = (head.issuerName or b"").decode("utf-8", "replace")
    der = ctypes.string_at(head.derCert.data, head.derCert.len)
    begin, end = ctypes.c_int64(), ctypes.c_int64()
    n["CERT_GetCertTimes"](cert, ctypes.byref(begin), ctypes.byref(end))
    return {
        "id": hashlib.sha256(der).hexdigest(),
        "name": _rdn(subject, "CN") or _rdn(subject, "O") or subject,
        "organization": _rdn(subject, "O"),
        "email": _rdn(subject, "E") or _rdn(subject, "MAIL"),
        "subject": subject,
        "issuer": _rdn(issuer, "CN") or _rdn(issuer, "O") or issuer,
        "selfSigned": subject == issuer,
        "validFrom": begin.value // 1000000,
        "validUntil": end.value // 1000000,
    }


def list_certificates(data):
    if not os.path.exists(os.path.join(data, FOLDER, "cert9.db")):
        return []
    with _Database(data) as db:
        found, listing = db.certificates()
        result = [info for info, _cert in found]
        if listing:
            _libs()["CERT_DestroyCertList"](listing)
    return sorted(result, key=lambda c: c["name"].lower())


@NICKNAME_CALLBACK
def _nickname(old, cancel, cert):
    """A certificate in the file has no name (often the CA certificates): make one, as pk12util does."""
    n = _libs()
    if not cert:
        return None
    nick = n["CERT_MakeCANickname"](cert)
    if not nick:
        return None
    if old and old.contents.data and ctypes.string_at(old.contents.data, old.contents.len) == ctypes.string_at(nick):
        return None  # the same name again: give up rather than loop
    item = n["SECITEM_AllocItem"](None, None, 0)
    if not item:
        return None
    item.contents.data = nick
    item.contents.len = len(ctypes.string_at(nick))
    return ctypes.cast(item, ctypes.c_void_p).value


def _decode(slot, blob, password_bytes):
    n = _libs()
    buffer = ctypes.create_string_buffer(password_bytes, len(password_bytes))
    item = SECItem(0, ctypes.cast(buffer, ctypes.c_void_p), len(password_bytes))
    dcx = n["SEC_PKCS12DecoderStart"](ctypes.byref(item), slot, None, None, None, None, None, None)
    if not dcx:
        raise CertError("Couldn't read the certificate (%s)." % _error_name())
    try:
        if n["SEC_PKCS12DecoderUpdate"](dcx, blob, len(blob)) != SEC_SUCCESS:
            return _error_name()
        if n["SEC_PKCS12DecoderVerify"](dcx) != SEC_SUCCESS:
            return _error_name()
        if n["SEC_PKCS12DecoderValidateBags"](dcx, _nickname) != SEC_SUCCESS:
            return _error_name()
        if n["SEC_PKCS12DecoderImportBags"](dcx) != SEC_SUCCESS:
            return _error_name()
        return None
    finally:
        n["SEC_PKCS12DecoderFinish"](dcx)


MESSAGES = {
    "SEC_ERROR_BAD_PASSWORD": "Wrong password for this certificate file.",
    "SEC_ERROR_PKCS12_DECODING_PFX": "This isn't a certificate file LibreOffice can use (.p12 or .pfx).",
    "SEC_ERROR_PKCS12_INVALID_MAC": "Wrong password for this certificate file.",
    "SEC_ERROR_BAD_DER": "This isn't a certificate file LibreOffice can use (.p12 or .pfx).",
    "SEC_ERROR_PKCS12_UNSUPPORTED_PBE_ALGORITHM": "This certificate file uses old encryption the phone doesn't accept. "
                                                  "Export it again from the program that made it.",
    "SEC_ERROR_PKCS12_UNSUPPORTED_MAC_ALGORITHM": "This certificate file uses old encryption the phone doesn't accept. "
                                                  "Export it again from the program that made it.",
}


def import_certificate(data, path, password):
    """Add the certificates and private key in a PKCS#12 file. Returns the signing certificates now
    in the store. The file itself isn't kept."""
    with open(path, "rb") as f:
        blob = f.read(1 << 20)
    if len(blob) >= 1 << 20 or not blob:
        raise CertError("This isn't a certificate file LibreOffice can use (.p12 or .pfx).")
    before = {c["id"] for c in list_certificates(data)}
    attempts = [password.encode("utf-16-be") + b"\0\0"]
    if not password:
        attempts.append(b"")  # some tools write an empty password as no password at all
    with _Database(data) as db:
        for attempt in attempts:
            error = _decode(db.slot, blob, attempt)
            if not error:
                break
            if error not in ("SEC_ERROR_BAD_PASSWORD", "SEC_ERROR_PKCS12_INVALID_MAC"):
                break
    if error:
        raise CertError(MESSAGES.get(error, "Couldn't add the certificate (%s)." % error))
    after = list_certificates(data)
    if not after:
        raise CertError("The file has no private key, so it can't be used to sign.")
    for c in after:
        c["new"] = c["id"] not in before
    return after


def remove_certificate(data, cert_id):
    """Delete a signing certificate and its private key."""
    with _Database(data) as db:
        found, listing = db.certificates()
        try:
            for info, cert in found:
                if info["id"] == cert_id:
                    if _libs()["PK11_DeleteTokenCertAndKey"](cert, None) != SEC_SUCCESS:
                        raise CertError("Couldn't remove the certificate (%s)." % _error_name())
                    return True
            return False
        finally:
            if listing:
                _libs()["CERT_DestroyCertList"](listing)


if __name__ == "__main__":
    import getpass
    import json
    import sys
    data = os.environ.get("CERTS_DATA") or os.path.join(
        os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share"), "libreoffice.juujbyg")
    if len(sys.argv) == 3 and sys.argv[1] == "add":
        print(json.dumps(import_certificate(data, sys.argv[2], getpass.getpass("Password: ")), indent=1))
    elif len(sys.argv) == 3 and sys.argv[1] == "remove":
        print(remove_certificate(data, sys.argv[2]))
    else:
        print(json.dumps(list_certificates(data), indent=1))
