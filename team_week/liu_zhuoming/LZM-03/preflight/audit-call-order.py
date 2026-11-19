#!/usr/bin/env python3
"""Source evidence for the fixed revision; presence never proves runtime order."""
import argparse
import hashlib
import json
import re
from pathlib import Path


def inspect(root):
    paths = ["src/bin/psql/common.cpp", "src/common/interfaces/libpq/fe-exec.cpp",
             "src/bin/psql/startup.cpp", "src/common/interfaces/libpq/Client_Sophos.cpp"]
    files = {p: (root / p).read_text(encoding="utf-8") for p in paths}
    client = "\n".join(files[p] for p in paths[:2])
    routed_module = "src/common/interfaces/libpq/row_request_v1.cpp"
    routing_present = bool(re.search(r"sse_v1::execute_request\s*\(", files[paths[0]]))
    if routing_present and (root / routed_module).is_file():
        files[routed_module] = (root / routed_module).read_text(encoding="utf-8")
        client += "\n" + files[routed_module]
    # Remove comments so examples/comments cannot count as call sites.
    code = re.sub(r"/\*.*?\*/|//[^\n]*", "", client, flags=re.S)
    calls = {name: bool(re.search(pattern, code)) for name, pattern in (
        ("preflight", r"\bpreflight\s*\("),
        ("begin_operation", r"\bbegin_(?:checked_)?operation\s*\("),
        ("complete_operation", r"\bcomplete_operation\s*\("),
    )}
    startup_gate = "v1 request routing requires the ZYC integration" in files[paths[2]]
    empty_guard = "sreq.token.empty() || sreq.derivation_key.empty()" in files[paths[1]]
    reinsert_guard = ("PQresultStatus(zyc_re_insert_res)" in files[paths[0]] and
                      "if (zyc_re_insert_res == NULL)" in files[paths[0]])
    evidence = []
    patterns = re.compile(r"begin_(?:checked_)?operation|complete_operation|preflight\(|"
                          r"search_request\(keyword\)|get_and_increment\(counter_map|"
                          r"sreq\.token\.empty|zyc_invalid_sse_request|"
                          r"zyc_re_insert_res|fides_query_search|v1 request routing|"
                          r"increment_v1\(keymap")
    for path, text in files.items():
        for number, line in enumerate(text.splitlines(), 1):
            if patterns.search(line):
                evidence.append({"path": path, "line": number, "text": line.strip()})
    gaps = ["client_missing_" + name for name, found in calls.items() if not found]
    if startup_gate:
        gaps.append("v1_startup_stops_before_business_routing")
    if not calls["preflight"]:
        gaps.append("production_empty_token_branch_has_no_state_health_classification")
    return {"result": "FAIL" if gaps else "REVIEW_REQUIRED", "exit_code": 3 if gaps else 4,
            "client_calls": calls, "startup_gate": startup_gate, "gsql_routes_to_request_module": routing_present,
            "existing_zyc_guards": {"empty_token_before_send": empty_guard,
                                    "reinsert_result_check": reinsert_guard},
            "gaps": gaps, "evidence": evidence,
            "source_sha256": {p: hashlib.sha256((root / p).read_bytes()).hexdigest() for p in paths},
            "note": "Static evidence only. Added calls require actual runtime traces before release."}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    result = inspect(Path(args.repo).resolve())
    Path(args.output).write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("CALL_ORDER=" + result["result"])
    for gap in result["gaps"]:
        print("GAP=" + gap)
    raise SystemExit(result["exit_code"])
