-- Synthetic envelopes with correct headers and lengths. The tags are test bytes.
\set ON_ERROR_STOP on
CREATE TABLE public.lmd_ordinary (rid text PRIMARY KEY, payload text);
INSERT INTO public.lmd_ordinary VALUES ('plain-01', 'ordinary');
fides_setup;
CREATE TABLE lmd_legacy (ind text, keyword text SSE='FIDES', payload text);
INSERT INTO lmd_legacy VALUES ('1', 'lmdlegacy', 'legacy fixture');
SELECT * FROM lmd_legacy WHERE keyword='lmdlegacy';

CREATE FUNCTION pg_temp.lmd_blob(purpose integer, payload_bytes integer)
RETURNS text LANGUAGE sql AS $$
    SELECT replace(encode(decode('53534531010' || $1, 'hex')
        || decode(repeat('00', 28), 'hex') || pg_catalog.int4send($2)
        || decode(repeat('31', $2 + 16), 'hex'), 'base64'), chr(10), '')
$$;

BEGIN;
SELECT public.sse_v1_create('public','lmd_create_rollback');
ROLLBACK;

DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM pg_tables WHERE schemaname = 'public'
               AND tablename IN ('lmd_create_rollback_data','lmd_create_rollback_index'))
       OR EXISTS (SELECT 1 FROM public.sse_v1_catalog WHERE logical_table = 'lmd_create_rollback') THEN
        RAISE EXCEPTION 'create rollback left artifacts';
    END IF;
    BEGIN
        PERFORM public.sse_v1_create('public','lmd_legacy');
        RAISE EXCEPTION 'legacy name was accepted';
    EXCEPTION WHEN duplicate_table THEN NULL;
    END;
    BEGIN
        PERFORM public.sse_v1_fetch('public','lmd_legacy',ARRAY[]::text[]);
        RAISE EXCEPTION 'unregistered legacy table was accepted';
    EXCEPTION WHEN undefined_table THEN NULL;
    END;
    BEGIN
        PERFORM public.sse_v1_create('public','Invalid.name');
        RAISE EXCEPTION 'invalid identifier was accepted';
    EXCEPTION WHEN invalid_parameter_value THEN NULL;
    END;
    IF NOT EXISTS (SELECT 1 FROM public.lmd_ordinary WHERE rid = 'plain-01' AND payload = 'ordinary') THEN
        RAISE EXCEPTION 'ordinary SQL failed';
    END IF;
    RAISE NOTICE 'PASS DB-01 create rollback, legacy fencing, identifier checks and ordinary SQL';
END;
$$;

SELECT public.sse_v1_create('public','lmd_sample');
DO $$
DECLARE
    rid_a text := repeat('a',32);
    rid_b text := repeat('b',32);
    rid_c text := repeat('c',32);
    rid_d text := repeat('d',32);
    token_a text := repeat('A',22) || '==';
    token_b text := repeat('B',22) || '==';
    token_c text := repeat('C',22) || '==';
    token_d text := repeat('D',22) || '==';
    token_e text := repeat('E',22) || '==';
    row_min text := pg_temp.lmd_blob(1,12);
    row_max text := pg_temp.lmd_blob(1,49164);
    position text := pg_temp.lmd_blob(2,17);
    actual text[];
    before_data text;
    before_index text;
    after_data text;
    after_index text;
BEGIN
    IF octet_length(row_min) <> 88 OR octet_length(row_max) <> 65624 OR octet_length(position) <> 96
       OR NOT EXISTS (SELECT 1 FROM public.sse_v1_catalog WHERE logical_schema = 'public'
                      AND logical_table = 'lmd_sample' AND protocol_version = 1) THEN
        RAISE EXCEPTION 'unexpected fixture length or missing catalog registration';
    END IF;
    PERFORM public.sse_v1_insert('public','lmd_sample', ARRAY[rid_a,rid_b], ARRAY[row_min,row_max],
                                 ARRAY[token_a,token_b], ARRAY[position,position]);
    SELECT array_agg(f.data_blob ORDER BY f.rid) INTO actual
    FROM public.sse_v1_fetch('public','lmd_sample',ARRAY[rid_b,rid_a]) AS f;
    IF actual IS DISTINCT FROM ARRAY[row_min,row_max] THEN RAISE EXCEPTION 'row bytes changed'; END IF;
    SELECT array_agg(p.locator_blob ORDER BY p.ut) INTO actual
    FROM public.sse_v1_take('public','lmd_sample',ARRAY[token_a,token_c,token_b]) AS p;
    IF actual IS DISTINCT FROM ARRAY[position,position] THEN RAISE EXCEPTION 'position bytes changed'; END IF;
    IF EXISTS (SELECT 1 FROM public.sse_v1_take('public','lmd_sample',ARRAY[token_a,token_c]))
       OR EXISTS (SELECT 1 FROM public.sse_v1_take('public','lmd_sample',ARRAY[]::text[]))
       OR EXISTS (SELECT 1 FROM public.sse_v1_fetch('public','lmd_sample',ARRAY[]::text[])) THEN
        RAISE EXCEPTION 'empty request or miss returned rows';
    END IF;
    PERFORM public.sse_v1_reinsert('public','lmd_sample',ARRAY[]::text[],ARRAY[]::text[]);
    PERFORM public.sse_v1_reinsert('public','lmd_sample',ARRAY[token_d,token_e],ARRAY[position,position]);
    RAISE NOTICE 'PASS synthetic envelopes round trip at 88 and 65624 bytes; 96 byte positions; take/miss/empty/reinsert';

    SELECT md5(string_agg(rid || ':' || md5(data_blob), ',' ORDER BY rid)) INTO before_data FROM public.lmd_sample_data;
    SELECT md5(string_agg(ut || ':' || md5(locator_blob), ',' ORDER BY ut)) INTO before_index FROM public.lmd_sample_index;
    BEGIN
        PERFORM public.sse_v1_insert('public','lmd_sample',ARRAY[rid_c,rid_a,rid_d],ARRAY[row_min,row_min,row_min],
            ARRAY[token_a,token_b,token_c],ARRAY[position,position,position]);
        RAISE EXCEPTION 'middle duplicate rid accepted';
    EXCEPTION WHEN unique_violation THEN NULL;
    END;
    BEGIN
        PERFORM public.sse_v1_insert('public','lmd_sample',ARRAY[rid_c,rid_d],ARRAY[row_min,row_min],
            ARRAY[token_a,token_d],ARRAY[position,position]);
        RAISE EXCEPTION 'index collision accepted';
    EXCEPTION WHEN unique_violation THEN NULL;
    END;
    BEGIN
        PERFORM public.sse_v1_insert('public','lmd_sample',ARRAY[rid_c],ARRAY[pg_temp.lmd_blob(1,49165)],
            ARRAY[token_a],ARRAY[position]);
        RAISE EXCEPTION 'oversize row accepted';
    EXCEPTION WHEN check_violation THEN NULL;
    END;
    BEGIN
        PERFORM public.sse_v1_insert('public','lmd_sample',ARRAY[rid_c],ARRAY[row_min],ARRAY[token_a],ARRAY[position || 'x']);
        RAISE EXCEPTION 'malformed locator accepted';
    EXCEPTION WHEN check_violation THEN NULL;
    END;
    BEGIN
        PERFORM public.sse_v1_insert('public','lmd_sample',ARRAY[rid_c],ARRAY[row_min],ARRAY[token_a],ARRAY[]::text[]);
        RAISE EXCEPTION 'different array lengths accepted';
    EXCEPTION WHEN invalid_parameter_value THEN NULL;
    END;
    BEGIN
        PERFORM public.sse_v1_insert('public','lmd_sample',ARRAY[rid_c],ARRAY[row_min],ARRAY[token_a],ARRAY[NULL::text]);
        RAISE EXCEPTION 'NULL array element accepted';
    EXCEPTION WHEN not_null_violation THEN NULL;
    END;
    BEGIN
        PERFORM public.sse_v1_fetch('public','lmd_sample',ARRAY[rid_a,rid_c]);
        RAISE EXCEPTION 'missing rid accepted';
    EXCEPTION WHEN cardinality_violation THEN NULL;
    END;
    BEGIN
        PERFORM public.sse_v1_fetch('public','lmd_sample',ARRAY[rid_a,rid_a]);
        RAISE EXCEPTION 'duplicate fetch rid accepted';
    EXCEPTION WHEN cardinality_violation THEN NULL;
    END;
    SELECT md5(string_agg(rid || ':' || md5(data_blob), ',' ORDER BY rid)) INTO after_data FROM public.lmd_sample_data;
    SELECT md5(string_agg(ut || ':' || md5(locator_blob), ',' ORDER BY ut)) INTO after_index FROM public.lmd_sample_index;
    IF before_data IS NULL OR before_index IS NULL OR before_data IS DISTINCT FROM after_data
       OR before_index IS DISTINCT FROM after_index OR (SELECT count(*) FROM public.lmd_sample_data) <> 2
       OR (SELECT count(*) FROM public.lmd_sample_index) <> 2 THEN RAISE EXCEPTION 'failed calls changed tables'; END IF;
    RAISE NOTICE 'PASS rollback and errors; data before/after %/%; index before/after %/%',
        before_data, after_data, before_index, after_index;
END;
$$;

-- Simulate corruption without leaving the disposable table changed.
BEGIN;
ALTER TABLE public.lmd_sample_data DROP CONSTRAINT lmd_sample_data_pkey;
DELETE FROM public.lmd_sample_data WHERE rid = repeat('b', 32);
INSERT INTO public.lmd_sample_data (rid, data_blob)
SELECT rid, data_blob FROM public.lmd_sample_data WHERE rid = repeat('a', 32);

DO $$
DECLARE
    requested text[] := ARRAY[repeat('a', 32), repeat('b', 32)];
BEGIN
    IF (SELECT count(*) FROM public.lmd_sample_data WHERE rid = ANY(requested)) <> 2
       OR (SELECT count(*) FROM public.lmd_sample_data WHERE rid = requested[1]) <> 2 THEN
        RAISE EXCEPTION 'fixture must contain a duplicate and a missing rid with the original row count';
    END IF;
    BEGIN
        PERFORM public.sse_v1_fetch('public', 'lmd_sample', requested);
        RAISE EXCEPTION 'equal total masked a duplicate and a missing rid';
    EXCEPTION WHEN cardinality_violation THEN
        RAISE NOTICE 'PASS fetch rejects equal total with duplicate and missing rid (SQLSTATE %)', SQLSTATE;
    END;
END;
$$;
ROLLBACK;
