"""Regenerate public v1 vectors with Python cryptography, separate from the C++ API."""
import base64
import pathlib
import struct

from cryptography.hazmat.primitives.ciphers.aead import AESGCM

SCOPE = (b"00112233445566778899aabbccddeeff", b"16384", b"public", b"week_demo")
RID = bytes.fromhex("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
KEY_ID = bytes(range(16))
KEYS = {1: bytes(range(32)), 2: bytes(range(32, 64))}


def fields(*items):
    return b"".join(struct.pack(">I", len(item)) + item for item in items)


def main():
    row = fields(b"17", "苹果".encode("utf-8"), b"two words\x00\x00")
    vectors = [
        ("row", 1, row),
        ("row_repeat", 1, row),
        ("locator", 2, RID + b"\x00"),
        ("row_bad_length", 1, struct.pack(">III", 0xffffffff, 0, 0)),
        ("locator_bad_op", 2, RID + b"\x01"),
    ]
    lines = ["name\taad_hex\tplaintext_hex\tblob_base64"]
    for i, (name, kind, plain) in enumerate(vectors):
        nonce = bytes(range(i * 12, (i + 1) * 12))
        header = b"SSE1" + bytes((1, kind)) + KEY_ID + nonce + struct.pack(">I", len(plain))
        aad = header + fields(*SCOPE) + (RID if kind == 1 else b"")
        blob = header + AESGCM(KEYS[kind]).encrypt(nonce, plain, aad)
        lines.append("\t".join((name, aad.hex(), plain.hex(), base64.b64encode(blob).decode("ascii"))))
    pathlib.Path(__file__).with_name("public-vectors.tsv").write_bytes(
        ("\n".join(lines) + "\n").encode("ascii"))


if __name__ == "__main__":
    main()
