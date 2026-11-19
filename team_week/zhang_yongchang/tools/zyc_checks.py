"""Fail-closed coverage checks shared by live cases and final evidence export."""
import json

def validate_request_coverage(requests, directory):
    calls_by_request = {}
    for request in requests:
        number = request['request']
        path = directory/('request-%02d.wire.jsonl' % number)
        calls = [json.loads(line) for line in path.read_text(encoding='utf-8').splitlines()] if path.exists() else []
        calls_by_request[number] = calls
        if request['exit'] != 0:
            continue
        if not path.exists() or not calls:
            raise ValueError('missing trace for successful request %s' % number)
        kind = request['expected_kind']
        events = request['events']
        def exactly(fragment):
            matched = [c for c in calls if fragment in c['sql']]
            if len(matched) != 1 or matched[0]['status'] != 'PGRES_TUPLES_OK':
                raise ValueError('request %s lacks one successful %s call' % (number, fragment))
        if kind == 'search':
            for fragment in ('fides_v1_select ', 'sse_v1_reinsert(', 'sse_v1_fetch('):
                exactly(fragment)
            required = ('search_take_sent','reinsert_sent','fetch_sent','rows_authenticated','search_commit','complete_durable')
        elif kind in ('create', 'insert'):
            exactly('sse_v1_'+kind+'(')
            required = (kind+'_sent','state_saved','complete_durable')
        elif kind == 'unknown':
            if any(any(part in c['sql'] for part in ('fides_v1_select ', 'sse_v1_insert(',
                    'sse_v1_reinsert(', 'sse_v1_fetch(', 'sse_v1_create(')) for c in calls):
                raise ValueError('unknown keyword sent a business request')
            required = ('unknown_empty',)
        else:
            raise ValueError('unclassified successful request %s' % number)
        if any(event not in events for event in required):
            raise ValueError('missing phase event for successful request %s' % number)
    return calls_by_request
