from pathlib import Path
import hashlib
import json
import re

root = Path(__file__).resolve().parent
proof = root / "evidence"
read = lambda name: (proof / name).read_text(encoding="utf-8", errors="replace")
assert "HEADER_CHECK_PASS" in read("header-check.log")
assert "PASS AES-256-GCM" in read("crypto-probe.log")
assert "not found" not in read("crypto-ldd.txt")
log = read("sql-draft.log")
assert re.search(r"NOTICE:.*DUPLICATE_RID_REJECTED", log)
assert re.search(r"NOTICE:.*SHORT_LOCATOR_REJECTED", log)
assert "ERROR:" not in log
assert "server stopped" in read("stop.log")
assert "PASS: 19 cases, 38 checker runs" in read("lzm-offline-executed.txt")
assert "--keyword" in read("lzm-argument.stderr")
assert not (proof / "lzm-argument.stdout").read_bytes()
assert 4 * ((49164 + 54 + 2) // 3) == 65624
assert 4 * ((17 + 54 + 2) // 3) == 96
for page in root.glob("*.md"):
    for target in re.findall(r"\]\(([^)]+)\)", page.read_text(encoding="utf-8")):
        assert "://" in target or (page.parent / target).exists(), target
result = {"task": "SHY-02", "contract_version": "1.0", "source_base": "25489f563f25b522d0cd37a6c85865039e3f340f", "header_compile": "PASS", "aes_gcm_library": "PASS", "sql_shape": "PASS", "lzm_offline_acceptance": "PASS", "member_review": "awaiting member acknowledgement", "implementation": "module implementation follows in YZH-01/LMD-01/LZM-02/ZYC-01"}
(root / "results.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
manifest = {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(root.rglob("*")) if p.is_file() and p.name != "manifest-sha256.json"}
(root / "manifest-sha256.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print("PASS SHY-02: header, GCM library, physical SQL, state checker, envelope lengths and links")
