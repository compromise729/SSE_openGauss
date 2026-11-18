from pathlib import Path
from collections import Counter
import hashlib
import json
import re

root = Path(__file__).resolve().parent
proof = root / "evidence"

def read(name):
    return (proof / name).read_text(encoding="utf-8", errors="replace")

def query_rows(log, query):
    start = log.index(query) + len(query)
    tail = log[start:]
    end = re.search(r"\(\d+ rows?\)", tail)
    assert end, query
    rows = []
    for line in tail[:end.start()].splitlines():
        cells = tuple(c.strip() for c in line.split("|"))
        if len(cells) == 3 and cells[0].isdigit():
            rows.append(cells)
    return Counter(rows)

apple = Counter([("1", "shy01_apple", "red apple"), ("2", "shy01_apple", "green apple")])
banana = Counter([("3", "shy01_banana", "yellow banana")])
query_a = "SELECT * FROM shy01_demo WHERE keyword='shy01_apple';"
query_b = "SELECT * FROM shy01_demo WHERE keyword='shy01_banana';"
search = read("02-search.log")
assert query_rows(search, query_a) == apple
assert query_rows(search, query_b) == banana
assert query_rows(search[search.index(query_a) + len(query_a):], query_a) == apple
assert query_rows(read("03-reconnect.log"), query_a) == apple
assert query_rows(read("03-reconnect.log"), "SELECT * FROM shy01_demo WHERE keyword='shy01_unknown';") == Counter()
assert "All of openGauss successfully made" in read("build.log")
assert "openGauss installation complete" in read("install.log")
assert "server stopped" in read("stop.log")
for name in ["gsql-ldd.txt", "gaussdb-ldd.txt"]:
    assert "not found" not in read(name)
assert "/opt/og/openGauss-server/dest//lib/libpq_ce.so.5" in read("gsql-ldd.txt") or "/opt/og/openGauss-server/dest/lib/libpq_ce.so.5" in read("gsql-ldd.txt")
assert "FAILED" not in read("patch-dry-run.log")
assert read("commit-delta-core.diff") == ""
assert "ISOLATED_SERVER_STOPPED=yes" in read("final-check.txt")
expected_counts = [(2, 1), (6, 2), (8, 2)]
for name, expected in zip(["01-write", "02-search", "03-reconnect"], expected_counts):
    counts = dict(line.rsplit(" ", 1) for line in read(name + "-counter.txt").splitlines())
    assert (int(counts["shy01_apple"]), int(counts["shy01_banana"])) == expected
results = {"source_commit": read("source-commit.txt").strip(), "build": "PASS", "install": "PASS", "apple": "PASS", "banana": "PASS", "repeat": "PASS", "reconnect": "PASS", "unknown": "PASS", "patch_dry_run": "PASS", "original_environment_unchanged": "PASS", "limitations": ["incremental rebuild from retained configured artifacts", "business rows remain plaintext", "index location readable", "member patches only dry-run, awaiting owner integration", "Mitra missing counter-file warnings during unrelated startup"]}
(root / "results.json").write_text(json.dumps(results, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
files = [p for p in root.rglob("*") if p.is_file() and p.name != "manifest-sha256.json"]
manifest = {str(p.relative_to(root)).replace("\\", "/"): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(files)}
(root / "manifest-sha256.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print("PASS: build/install, full-row results, repeated/reopened searches, counter snapshots, patch applicability and environment preservation")
