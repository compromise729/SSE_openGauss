-- Public synthetic envelopes exercise the database wire limits.
-- These structural tags do not represent authenticated YZH-01 ciphertext.
\set ON_ERROR_STOP on
BEGIN;
DO $$
DECLARE
    rid text := 'fedcba9876543210fedcba9876543210';
    token text := 'AAAAAAAAAAAAAAAAAAAAAA==';
    envelope text;
    position text;
    received text;
BEGIN
    envelope := replace(encode(decode('535345310101' || repeat('00',28), 'hex')
        || pg_catalog.int4send(49164) || decode(repeat('31',49164+16),'hex'),'base64'),chr(10),'');
    position := replace(encode(decode('535345310102' || repeat('00',28), 'hex')
        || pg_catalog.int4send(17) || decode(repeat('31',17+16),'hex'),'base64'),chr(10),'');
    IF octet_length(envelope) <> 65624 THEN RAISE EXCEPTION 'fixture length differs'; END IF;
    PERFORM public.sse_v1_insert('public','week_demo',ARRAY[rid],ARRAY[envelope],ARRAY[token],ARRAY[position]);
    SELECT f.data_blob INTO received FROM public.sse_v1_fetch('public','week_demo',ARRAY[rid]) AS f;
    IF received IS DISTINCT FROM envelope THEN RAISE EXCEPTION 'max envelope truncated'; END IF;
    RAISE NOTICE 'PASS synthetic database cap 65624 bytes and non-integer rid round trip';
    BEGIN
        PERFORM public.sse_v1_insert('public','week_demo',ARRAY[repeat('e',32)],
            ARRAY[envelope || 'AAAA'],ARRAY['BBBBBBBBBBBBBBBBBBBBBB=='],ARRAY[position]);
        RAISE EXCEPTION 'oversized envelope accepted';
    EXCEPTION WHEN check_violation THEN
        RAISE NOTICE 'PASS synthetic over-cap envelope rejected SQLSTATE %', SQLSTATE;
    END;
END;
$$;
ROLLBACK;
