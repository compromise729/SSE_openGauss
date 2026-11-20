#!/usr/bin/env python3
"""SHY-04 real gsql database failure and completeness regressions."""
from collections import Counter
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

repo = Path(os.environ["OPENGAUSS_PROJECT_ROOT"])
run = Path(os.environ["ISOLATED_RUN_DIR"])
build = Path(os.environ["LMD03_BUILD"])
state = run / "evidence/lmd03-state"
output = run / "evidence/lmd03"
output.mkdir(mode=0o700)
common = ["-X", "-q", "-A", "-t", "-F", "|", "-h", str(run / "socket"),
          "-p", os.environ["ISOLATED_PORT"], "-d", "postgres", "-U", "omm",
          "-v", "ON_ERROR_STOP=1"]
records = []
requests = []
sequence = 0
apple = [("1", "apple", "red apple"), ("2", "apple", "green apple")]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def quote(value):
    return "'" + value.replace("'", "''") + "'"


def clean_environment():
    env = dict(os.environ)
    for name in ("SSE_V1_STATE_DIR", "SSE_V1_MODE", "LD_PRELOAD", "STATE_FAULT",
                 "ZYC_TRACE_FILE", "ZYC_PUBLIC_FIXTURE"):
        env.pop(name, None)
    return env


def raw(sql, label=None):
    result = subprocess.run([str(repo / "dest/bin/gsql")] + common + ["-f", "-"],
                            input=sql.encode("utf-8"), env=clean_environment(),
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=90)
    if label:
        (output / (label + ".sql")).write_text(sql, encoding="utf-8")
        (output / (label + ".stdout")).write_bytes(result.stdout)
        (output / (label + ".stderr")).write_bytes(result.stderr)
    require(result.returncode == 0, "inspection SQL failed: " + result.stderr.decode(errors="replace"))
    return result.stdout.decode("utf-8").splitlines()


def snapshot_server():
    lines = raw("""SELECT 'SNAP=' || row_to_json(s)::text FROM (
        SELECT (SELECT count(*) FROM public.week_demo_data) AS data_rows,
               (SELECT count(*) FROM public.week_demo_index) AS index_rows,
               (SELECT md5(string_agg(rid || ':' || data_blob, ',' ORDER BY rid, data_blob))
                FROM public.week_demo_data) AS data_digest,
               (SELECT md5(string_agg(ut || ':' || locator_blob, ',' ORDER BY ut, locator_blob))
                FROM public.week_demo_index) AS index_digest) AS s;""")
    return json.loads(next(line[5:] for line in lines if line.startswith("SNAP=")))


def snapshot_files(directory):
    return {file.name: file.read_bytes() for file in directory.iterdir() if file.is_file()}


def public_state(directory):
    counters = directory / "fides_client.mat"
    return {"marker_present": (directory / "operation.v1").exists(),
            "counters": counters.read_text().splitlines() if counters.exists() else [],
            "files": sorted(file.name for file in directory.iterdir() if file.is_file())}


def row_multiset(process):
    return Counter(line for line in process.stdout.decode("utf-8").splitlines() if "|" in line)


def client(sql, directory=None):
    global sequence
    sequence += 1
    directory = directory or state
    prefix = "request-{:02d}".format(sequence)
    trace = output / (prefix + ".wire.jsonl")
    env = dict(os.environ, SSE_V1_STATE_DIR=str(directory), STATE_TRACE="1",
               LD_PRELOAD=str(build / "trace.so"), ZYC_TRACE_FILE=str(trace),
               ZYC_PUBLIC_FIXTURE="1")
    env.pop("STATE_FAULT", None)
    process = subprocess.run([str(repo / "dest/bin/gsql")] + common + ["-f", "-"],
                             input=sql.encode("utf-8"), env=env,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=90)
    (output / (prefix + ".sql")).write_text(sql, encoding="utf-8")
    (output / (prefix + ".stdout")).write_bytes(process.stdout)
    (output / (prefix + ".stderr")).write_bytes(process.stderr)
    events = [line[len("STATE_EVENT "):] for line in process.stderr.decode(errors="replace").splitlines()
              if line.startswith("STATE_EVENT ")]
    calls = [json.loads(line) for line in trace.read_text().splitlines()] if trace.exists() else []
    requests.append({"request": sequence, "exit_code": process.returncode,
                     "events": events, "trace": trace.name})
    return process, events, calls


def succeeded(process):
    require(process.returncode == 0, process.stderr.decode(errors="replace"))


def expect_rows(keyword, rows, directory=None):
    process, events, calls = client("SELECT * FROM week_demo WHERE keyword=" + quote(keyword) + ";", directory)
    succeeded(process)
    require(row_multiset(process) == Counter("|".join(row) for row in rows), "complete row multiset differs")
    return process, events, calls


def copy_state(name):
    target = run / "evidence" / name
    shutil.copytree(str(state), str(target))
    return target


def decode(kind, values):
    process = subprocess.run([str(build / "decode"), str(state), "week_demo", kind],
                             input=("\n".join(values) + "\n").encode("utf-8"),
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
    succeeded(process)
    return [json.loads(line) for line in process.stdout.decode("utf-8").splitlines()]


def arrays(value):
    require(value.startswith("{") and value.endswith("}"), "invalid bound array")
    return json.loads("[" + value[1:-1] + "]")


def case(name, action):
    item = {"case": name, "result": "FAIL"}
    start = sequence
    try:
        item.update(action() or {})
        item["result"] = "PASS"
        print("PASS " + name, flush=True)
    except Exception as error:
        item["reason"] = str(error)
        print("FAIL {}: {}".format(name, error), flush=True)
        raise
    finally:
        item["requests"] = list(range(start + 1, sequence + 1))
        records.append(item)
        result = {"task": "LMD-03", "source_commit": "5c8b2b03b482e203ac65a6f989a1b669126f263c",
                  "result": "PASS" if all(row["result"] == "PASS" for row in records) else "FAIL",
                  "cases": records, "requests": requests}
        (output / "results.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n",
                                             encoding="utf-8")


def create_and_insert():
    process, _, _ = client("CREATE TABLE week_demo (ind text, keyword text SSE='FIDES', payload text) "
                           "WITH (sse_storage='ROW_AEAD_V1');")
    succeeded(process)
    process, _, _ = client("INSERT INTO week_demo VALUES "
                           "('1','apple','red apple'),('2','apple','green apple'),('3','banana','yellow banana');")
    succeeded(process)
    data = [line[5:].split("|") for line in raw("SELECT 'DATA='||rid||'|'||data_blob FROM public.week_demo_data ORDER BY rid;")
            if line.startswith("DATA=")]
    index = [line[4:].split("|") for line in raw("SELECT 'IDX='||ut||'|'||locator_blob FROM public.week_demo_index ORDER BY ut;")
             if line.startswith("IDX=")]
    require(len(data) == len(index) == 3, "initial physical row count")
    require(all(re.fullmatch("[0-9a-f]{32}", rid) for rid, _ in data)
            and any(not rid.isdigit() for rid, _ in data),
            "fixture must exercise non-integer full rid")
    rows = decode("row", ["\t".join(row) for row in data])
    positions = decode("locator", [row[1] for row in index])
    require(Counter((r["ind"], r["keyword"], r["payload"]) for r in rows) ==
            Counter(apple + [("3", "banana", "yellow banana")]), "physical row decrypt differs")
    require(Counter(row["rid"] for row in positions) == Counter(rid for rid, _ in data)
            and all(row["operation"] == 0 for row in positions), "index identities differ")
    evidence = {"rows": rows, "positions": positions,
                "physical_data": data, "physical_index": index}
    (output / "physical-public-fixture.json").write_text(json.dumps(evidence, ensure_ascii=False, indent=2) + "\n",
                                                        encoding="utf-8")
    columns = raw("SELECT 'COL='||table_name||':'||column_name FROM information_schema.columns "
                  "WHERE table_schema='public' AND table_name IN ('week_demo_data','week_demo_index') "
                  "ORDER BY table_name,ordinal_position;")
    require([line for line in columns if line.startswith("COL=")] ==
            ["COL=week_demo_data:rid", "COL=week_demo_data:data_blob",
             "COL=week_demo_index:ut", "COL=week_demo_index:locator_blob"], "unexpected business columns")
    return {"database": snapshot_server(), "non_integer_rids": [row[0] for row in data],
            "authenticated_row_and_locator_match": True}


def repeat_search():
    before = snapshot_server()
    identities = set()
    phases = []
    for _ in range(2):
        _, events, calls = expect_rows("apple", apple)
        hit = next(call for call in calls if call["sql"].startswith("fides_v1_select "))
        fetched = next(call for call in calls if "sse_v1_fetch(" in call["sql"])
        reinserted = next(call for call in calls if "sse_v1_reinsert(" in call["sql"])
        recovered = decode("locator", [row[1] for row in hit["rows"]])
        rids = {row["rid"] for row in recovered}
        require(len(rids) == 2 and set(arrays(fetched["params"][1])) == rids ==
                {row[0] for row in fetched["rows"]}, "Fides hit/fetch sets differ")
        refreshed = decode("locator", arrays(reinserted["params"][2]))
        require({row["rid"] for row in refreshed} == rids, "reinsert changed rid identities")
        if identities:
            require(rids == identities, "second reinsert changed identities")
        identities = rids
        require(all(event in events for event in ("rows_authenticated", "search_commit", "complete_durable")),
                "successful search phases incomplete")
        phases.append({"rids": sorted(rids), "search_rows": hit["rows"],
                       "fetch_rids": [row[0] for row in fetched["rows"]]})
    after = snapshot_server()
    require(before["data_digest"] == after["data_digest"] and before["data_rows"] == after["data_rows"] == 3
            and before["index_rows"] == after["index_rows"] == 3, "reinsert changed business data")
    expect_rows("banana", [("3", "banana", "yellow banana")])
    return {"before": before, "after": after, "two_reinserts": phases}


def failure_result(directory, process, events, error, server_before, state_before):
    require(process.returncode != 0 and error in process.stderr, "expected error did not propagate")
    require(not row_multiset(process), "failed operation displayed plaintext")
    require((directory / "operation.v1").exists(), "incomplete marker absent")
    after = snapshot_server()
    require(after == server_before, "database statement/transaction left partial writes")
    files_after = snapshot_files(directory)
    require(all(files_after.get(name) == content for name, content in state_before.items()),
            "failed operation changed persisted input state")
    process_again, reopen_events, _ = client("SELECT * FROM week_demo WHERE keyword='apple';", directory)
    require(process_again.returncode != 0 and b"incomplete_operation" in process_again.stderr,
            "client reopen after failure was not blocked")
    require(not row_multiset(process_again), "blocked reopen printed business data")
    require("search_take_sent" not in reopen_events, "blocked reopen contacted search")
    return {"before": server_before, "after": after, "exit_code": process.returncode,
            "reopen_exit_code": process_again.returncode, "events": events,
            "persisted_input_files_unchanged": True, "failure_marker_retained": True,
            "no_partial_plaintext": True, "state_after": public_state(directory)}


def injected_write(kind):
    directory = copy_state("lmd03-" + kind)
    count = snapshot_server()["data_rows"]
    if kind == "duplicate-rid":
        table = "week_demo_data"
        body = ("IF (SELECT count(*) FROM public.week_demo_data) > {count} THEN "
                "NEW.rid := (SELECT rid FROM public.week_demo_data ORDER BY rid LIMIT 1); END IF;")
    elif kind == "second-row":
        table = "week_demo_data"
        body = ("IF (SELECT count(*) FROM public.week_demo_data) > {count} THEN "
                "RAISE EXCEPTION 'LMD03 second row failure'; END IF;")
    else:
        table = "week_demo_index"
        body = "NEW.ut := (SELECT ut FROM public.week_demo_index ORDER BY ut LIMIT 1);"
    raw("CREATE FUNCTION public.lmd03_inject() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN " +
        body.format(count=count) + " RETURN NEW; END $$; CREATE TRIGGER lmd03_inject "
        "BEFORE INSERT ON public." + table + " FOR EACH ROW EXECUTE PROCEDURE public.lmd03_inject();")
    before, files = snapshot_server(), snapshot_files(directory)
    try:
        process, events, calls = client("INSERT INTO week_demo VALUES "
                                       "('new1','apple','first'),('new2','apple','second');", directory)
        require("insert_sent" in events, "write was not sent through the real client")
        require(any("sse_v1_insert(" in call["sql"] and call["status"] == "PGRES_FATAL_ERROR"
                    for call in calls), "database insert did not fail")
        if kind in ("duplicate-rid", "index-collision"):
            require(b"duplicate key" in process.stderr, "unique constraint error absent")
        return failure_result(directory, process, events, b"db_failed", before, files)
    finally:
        raw("DROP TRIGGER lmd03_inject ON public." + table + "; DROP FUNCTION public.lmd03_inject();")


def reinsert_failure():
    directory = copy_state("lmd03-reinsert-failure")
    # Apple take removes two index rows, leaving one banana. Fail on the second reinsert.
    raw("""CREATE FUNCTION public.lmd03_inject() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN
        IF (SELECT count(*) FROM public.week_demo_index) > 1 THEN
            RAISE EXCEPTION 'LMD03 second reinsert failure';
        END IF;
        RETURN NEW; END $$;
        CREATE TRIGGER lmd03_inject BEFORE INSERT ON public.week_demo_index
        FOR EACH ROW EXECUTE PROCEDURE public.lmd03_inject();""")
    before, files = snapshot_server(), snapshot_files(directory)
    try:
        process, events, calls = client("SELECT * FROM week_demo WHERE keyword='apple';", directory)
        require("search_take_sent" in events and "reinsert_sent" in events and "fetch_sent" not in events,
                "reinsert failure occurred at unexpected phase")
        require(any("sse_v1_reinsert(" in call["sql"] and call["status"] == "PGRES_FATAL_ERROR"
                    for call in calls), "reinsert did not fail in database")
        require(any(call["sql"] == "ROLLBACK" for call in calls), "search did not roll back")
        return failure_result(directory, process, events, b"db_failed", before, files)
    finally:
        raw("DROP TRIGGER lmd03_inject ON public.week_demo_index; DROP FUNCTION public.lmd03_inject();")


def missing_row():
    directory = copy_state("lmd03-missing-row")
    item = next(row for row in decode("row", [
        line[5:].replace("|", "\t", 1) for line in raw("SELECT 'DATA='||rid||'|'||data_blob FROM public.week_demo_data;")
        if line.startswith("DATA=")]) if row["keyword"] == "apple")
    saved = next(line[5:].split("|") for line in raw("SELECT 'DATA='||rid||'|'||data_blob "
                 "FROM public.week_demo_data WHERE rid=" + quote(item["rid"]) + ";") if line.startswith("DATA="))
    original = snapshot_server()
    raw("DELETE FROM public.week_demo_data WHERE rid=" + quote(saved[0]) + ";")
    before, files = snapshot_server(), snapshot_files(directory)
    try:
        process, events, calls = client("SELECT * FROM week_demo WHERE keyword='apple';", directory)
        require(all(event in events for event in ("search_take_sent", "reinsert_sent", "fetch_sent")),
                "missing-row case did not use real Fides hit and fetch")
        result = failure_result(directory, process, events, b"inconsistent_result", before, files)
        result["before_fault"] = original
        result["injected_missing_rid"] = saved[0]
    finally:
        raw("INSERT INTO public.week_demo_data VALUES(" + quote(saved[0]) + "," + quote(saved[1]) + ");")
    require(snapshot_server() == original, "fixture cleanup did not restore original rows")
    return result


def equal_total_corruption():
    original = snapshot_server()
    data = [line[5:].split("|") for line in raw("SELECT 'DATA='||rid||'|'||data_blob "
            "FROM public.week_demo_data ORDER BY rid;") if line.startswith("DATA=")]
    repeated, missing = data[0], data[1]
    raw("ALTER TABLE public.week_demo_data DROP CONSTRAINT week_demo_data_pkey;"
        "DELETE FROM public.week_demo_data WHERE rid=" + quote(missing[0]) + ";"
        "INSERT INTO public.week_demo_data VALUES(" + quote(repeated[0]) + "," + quote(repeated[1]) + ");")
    try:
        sql = ("DO $$ BEGIN BEGIN PERFORM public.sse_v1_fetch('public','week_demo',ARRAY[" +
               quote(repeated[0]) + "," + quote(missing[0]) + "]); "
               "RAISE EXCEPTION 'invalid rid set accepted'; EXCEPTION WHEN cardinality_violation THEN "
               "RAISE NOTICE 'PASS exact rid coverage SQLSTATE %',SQLSTATE; END; END $$;")
        raw(sql, "equal-total-fetch")
        directory = copy_state("lmd03-broken-primary-key")
        before = snapshot_files(directory)
        process, events, _ = client("SELECT * FROM week_demo WHERE keyword='apple';", directory)
        require(process.returncode != 0 and b"state_invalid" in process.stderr, "client accepted corrupted table shape")
        require("search_take_sent" not in events and before == snapshot_files(directory),
                "bad schema advanced client state")
        return {"before": original, "corrupted": snapshot_server(), "direct_fetch_sqlstate": "21000",
                "client_status": "state_invalid", "client_exit_code": process.returncode}
    finally:
        raw("DELETE FROM public.week_demo_data WHERE rid=" + quote(repeated[0]) + ";"
            "INSERT INTO public.week_demo_data VALUES(" + quote(repeated[0]) + "," + quote(repeated[1]) + "),(" +
            quote(missing[0]) + "," + quote(missing[1]) + ");"
            "ALTER TABLE public.week_demo_data ADD PRIMARY KEY(rid);")
        require(snapshot_server() == original, "corruption fixture did not restore rows")


def boundary_tests():
    before = snapshot_server()
    sql = (repo / "team_week/li_mengde/LMD-03/db-boundaries.sql").read_text()
    raw(sql, "db-boundaries")
    require(snapshot_server() == before, "synthetic boundary test changed physical rows")
    row = ("i" * 16384, "k" * 1024, "p" * 16384)
    process, _, _ = client("INSERT INTO week_demo VALUES (" + ",".join(quote(value) for value in row) + ");")
    succeeded(process)
    expect_rows(row[1], [row])
    require("SIZE=45144" in raw("SELECT 'SIZE='||max(octet_length(data_blob)) FROM public.week_demo_data;"),
            "max real ciphertext was truncated")
    return {"synthetic_database_cap": 65624, "max_real_envelope": 45144,
            "field_bytes": [16384, 1024, 16384], "exact_client_recovery": True}


def oversize_rejected():
    examples = [("i" * 16385, "apple", ""), ("i", "k" * 1025, ""), ("i", "apple", "p" * 16385)]
    for row in examples:
        before, files = snapshot_server(), snapshot_files(state)
        process, events, _ = client("INSERT INTO week_demo VALUES (" + ",".join(quote(value) for value in row) + ");")
        require(process.returncode != 0 and not row_multiset(process), "oversize SQL did not fail")
        require("begin_durable" not in events and snapshot_server() == before and snapshot_files(state) == files,
                "oversize SQL changed state")
    return {"rejected_inputs": 3, "no_network_or_counter_effects": True}


def audit():
    before, files = snapshot_server(), snapshot_files(state)
    env = clean_environment()
    args = [str(repo / "dest/bin/gsql")] + common + ["-v", "target_schema=public", "-v", "target=week_demo",
            "-v", "profile=fides_v1", "-f", str(repo / "team_week/li_mengde/LMD-01/李孟德-check.sql")]
    for attempt in range(2):
        process = subprocess.run([arg.encode("utf-8") for arg in args], env=env,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
        (output / ("audit-{}.stdout".format(attempt))).write_bytes(process.stdout)
        (output / ("audit-{}.stderr".format(attempt))).write_bytes(process.stderr)
        succeeded(process)
        require(b"PASS fides_v1 structure and registration" in process.stderr, "audit skipped required check")
        require(b"FAIL " not in process.stderr and b"SKIP " not in process.stderr, "audit false positive")
    require(before == snapshot_server() and files == snapshot_files(state), "read-only audit changed state")
    raw("CREATE TABLE public.lmd03_plain(id int PRIMARY KEY, message text); "
        "INSERT INTO public.lmd03_plain VALUES (1,'ordinary SQL');")
    require("ordinary SQL" in raw("SELECT message FROM public.lmd03_plain WHERE id=1;"), "ordinary SQL failed")
    return {"before": before, "after": snapshot_server(), "state_files_unchanged": True,
            "ordinary_sql": True}


case("DB-01-real-user-create-and-ciphertext", create_and_insert)
case("DB-02-two-reinserts-preserve-rid", repeat_search)
case("DB-03-middle-duplicate-rid-rollback", lambda: injected_write("duplicate-rid"))
case("DB-04-second-business-row-rollback", lambda: injected_write("second-row"))
case("DB-05-index-write-collision-rollback", lambda: injected_write("index-collision"))
case("DB-06-second-reinsert-failure-rollback", reinsert_failure)
case("DB-07-real-hit-with-missing-data", missing_row)
case("DB-08-equal-total-corruption", equal_total_corruption)
case("DB-09-physical-and-real-envelope-limits", boundary_tests)
case("DB-10-client-oversize-before-effects", oversize_rejected)
case("DB-11-read-only-profile-and-ordinary-sql", audit)
print("LMD03_REQUIRED_CASES=PASS COUNT={}".format(len(records)), flush=True)
