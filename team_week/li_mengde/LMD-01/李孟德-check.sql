-- gsql -X -v target_schema=public -v target=lmd_demo -v profile=fides -f 李孟德-check.sql
-- Profiles: fides_v1, fides, multi, tpch, diana, januspp, orion, mitra, fbdsse, dual, closefb.
\set ON_ERROR_STOP on
BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ READ ONLY;
SELECT current_database() AS database, current_user AS connected_user,
       :'target_schema' AS target_schema, :'target' AS target, :'profile' AS profile;
SET LOCAL lmd.target_schema TO :'target_schema';
SET LOCAL lmd.target TO :'target';
SET LOCAL lmd.profile TO :'profile';

DO $$
DECLARE
    ns text := current_setting('lmd.target_schema');
    target text := current_setting('lmd.target');
    profile text := current_setting('lmd.profile');
    function_names text[];
    signatures text[];
    tables text[] := ARRAY[target || '_data', target || '_index'];
    expected text[] := ARRAY['ind,keyword,payload', 'ut,en_ind,join_col'];
    key_col text := 'ind';
    actual text;
    table_name text;
    qualified text;
    columns text;
    valid boolean := true;
    relation oid;
    primary_key_ok boolean;
    row_count bigint;
    duplicate record;
    details record;
    i integer;
BEGIN
    CASE profile
        WHEN 'fides_v1' THEN
            IF ns <> 'public' THEN
                RAISE EXCEPTION 'FAIL fides_v1 requires public schema';
            END IF;
            function_names := ARRAY['sse_v1_create','sse_v1_insert','sse_v1_take','sse_v1_reinsert','sse_v1_fetch'];
            signatures := ARRAY['text, text','text, text, text[], text[], text[], text[]',
                'text, text, text[]','text, text, text[], text[]','text, text, text[]'];
            expected := ARRAY['rid,data_blob','ut,locator_blob'];
            key_col := 'rid';
        WHEN 'fides', 'multi' THEN
            function_names := ARRAY['sse_create_tables2','fides_insert','fides_re_insert'];
            signatures := ARRAY['text, text, text, text','text[], text[], text[], text, text','text[], text[], text[], text'];
            IF profile = 'multi' THEN expected[1] := 'ind,keyword,payload,note'; END IF;
        WHEN 'mitra' THEN
            function_names := ARRAY['sse_create_tables2','mitra_insert','mitra_re_insert'];
            signatures := ARRAY['text, text, text, text','text[], text[], text[], text, text','text[], text[], text[], text'];
        WHEN 'fbdsse' THEN
            function_names := ARRAY['fbdsse_create_tables2','fbdsse_insert'];
            signatures := ARRAY['text, text, text, text','text[], text[], text[], text, text'];
            expected[2] := 'ut,en_ind,fb_ct';
        WHEN 'dual' THEN
            function_names := ARRAY['dual_create_tables2','dual_insert','dual_select'];
            signatures := ARRAY['text, text, text, text','text[], text[], text[], text, text','text[], text'];
            expected[2] := 'label1,label2,data';
        WHEN 'closefb' THEN
            function_names := ARRAY['closefb_create_tables2','closefb_insert','closefb_select'];
            signatures := ARRAY['text, text, text, text','text[], text[], text, text','text[], text'];
            expected[2] := 'indexkey,indexvalue';
        WHEN 'januspp' THEN
            function_names := ARRAY['januspp_create_tables2','januspp_insert','januspp_select'];
            signatures := ARRAY['text, text, text, text','text[], text[], text[], text, text','text[], text'];
            tables := ARRAY[target || '_data', target || '_indexadd', target || '_indexdel', target || '_cache'];
            expected := ARRAY['ind,keyword,payload','enckeyword,encind,tag','enckeyword,punckey,deltag','tkn,ind,tag'];
        WHEN 'orion' THEN
            function_names := ARRAY['orion_create_tables2'];
            signatures := ARRAY['text, text, text, text'];
            tables := ARRAY[target || '_data'];
            expected := ARRAY['ind,keyword,payload'];
            RAISE NOTICE 'INFO Orion index state is external to SQL; check the isolated orion_state.bin separately';
        WHEN 'diana' THEN
            function_names := ARRAY['diana_insert_bulk'];
            signatures := ARRAY['text[], text[], text[], text[], text[]'];
            tables := ARRAY[target || '_datatable', target || '_indextable'];
            expected := ARRAY['ind,keyword,values','ut,ind'];
        WHEN 'tpch' THEN
            function_names := ARRAY['sse_order_create_tables','fides_tpch_insert','fides_tpch_select'];
            signatures := ARRAY['text, text, text, text, text, text, text, text, text, text',
                'text[], text[], text[], text[], text[], text[], text[], text[], text[], text[], text[], text',
                'character varying[], character varying[], character varying[], text'];
            expected[1] := 'o_orderkey,o_custkey,o_orderstatus,o_totalprice,o_orderdate,o_orderpriority,o_clerk,o_shippriority,o_comment';
            key_col := 'o_orderkey';
        ELSE
            RAISE EXCEPTION 'Unknown inspection profile: %', profile;
    END CASE;

    FOR i IN 1..array_length(function_names, 1) LOOP
        SELECT min(oidvectortypes(p.proargtypes)) INTO actual
        FROM pg_proc p JOIN pg_namespace n ON n.oid = p.pronamespace
        WHERE n.nspname = ns AND p.proname = function_names[i]
              AND oidvectortypes(p.proargtypes) = signatures[i];
        IF actual IS NULL THEN
            valid := false;
            RAISE NOTICE 'FAIL function %.% expected (%) actual missing', ns, function_names[i], signatures[i];
        ELSE
            RAISE NOTICE 'PASS function %.% expected (%) actual (%)', ns, function_names[i], signatures[i], actual;
        END IF;
    END LOOP;

    FOR i IN 1..array_length(tables, 1) LOOP
        table_name := tables[i];
        qualified := format('%I.%I', ns, table_name);
        SELECT (SELECT c.oid FROM pg_class c JOIN pg_namespace n ON n.oid = c.relnamespace
                WHERE n.nspname = ns AND c.relname = table_name AND c.relkind = 'r') INTO relation;
        IF relation IS NULL THEN
            valid := false;
            RAISE NOTICE 'FAIL table % expected ordinary table actual missing', qualified;
            CONTINUE;
        END IF;
        RAISE NOTICE 'PASS table % expected ordinary table actual present', qualified;
        SELECT string_agg(attname || ':' || format_type(atttypid, atttypmod), ',' ORDER BY attnum)
        INTO actual FROM pg_attribute WHERE attrelid = relation AND attnum > 0 AND NOT attisdropped;
        columns := replace(expected[i], ',', ':text,') || ':text';
        IF actual IS DISTINCT FROM columns THEN
            valid := false;
            RAISE NOTICE 'FAIL columns % expected [%] actual [%]', qualified, columns, actual;
        ELSE
            RAISE NOTICE 'PASS columns % expected [%] actual [%]', qualified, columns, actual;
        END IF;
        IF profile = 'fides_v1' THEN
            SELECT EXISTS (
                SELECT 1 FROM pg_constraint c JOIN pg_attribute a ON a.attrelid = c.conrelid
                WHERE c.conrelid = relation AND c.contype = 'p' AND c.conkey = ARRAY[a.attnum]
                      AND a.attname = split_part(expected[i], ',', 1)
            ) AND NOT EXISTS (
                SELECT 1 FROM pg_attribute a
                WHERE a.attrelid = relation AND a.attnum > 0 AND NOT a.attisdropped AND NOT a.attnotnull
            ) INTO primary_key_ok;
            IF NOT primary_key_ok THEN
                valid := false;
                RAISE NOTICE 'FAIL primary key or nullable columns in %', qualified;
            END IF;
        END IF;
    END LOOP;

    IF NOT valid THEN
        RAISE EXCEPTION 'FAIL required schema checks failed, rows not inspected';
    END IF;
    IF profile = 'fides_v1' THEN
        PERFORM public.sse_v1_require_table(ns, target);
        FOR i IN 1..2 LOOP
            qualified := format('%I.%I', ns, tables[i]);
            EXECUTE format('SELECT count(*) AS rows, max(octet_length(%1$I)) AS max_cipher_bytes,
                          count(CASE WHEN NOT public.sse_v1_blob_valid(%1$I, %2$s) THEN 1 END) AS invalid
                          FROM %3$s', split_part(expected[i], ',', 2), i, qualified) INTO details;
            IF details.invalid > 0 THEN
                RAISE EXCEPTION 'FAIL % invalid envelopes %', qualified, details.invalid;
            END IF;
            RAISE NOTICE 'PASS % rows % max_cipher_bytes % invalid envelopes 0', qualified, details.rows, details.max_cipher_bytes;
        END LOOP;
        RAISE NOTICE 'PASS fides_v1 structure and registration';
        RETURN;
    END IF;
    FOR i IN 1..array_length(tables, 1) LOOP
        qualified := format('%I.%I', ns, tables[i]);
        EXECUTE format('SELECT count(*) FROM %s', qualified) INTO row_count;
        RAISE NOTICE 'ROWS % actual %', qualified, row_count;
    END LOOP;

    qualified := format('%I.%I', ns, tables[1]);
    FOR details IN EXECUTE format('SELECT row_to_json(d)::text AS value FROM %s d ORDER BY %I, row_to_json(d)::text LIMIT 20', qualified, key_col) LOOP
        RAISE NOTICE 'DATA % %', qualified, details.value;
    END LOOP;
    valid := true;
    FOR duplicate IN EXECUTE format('SELECT %1$I AS id, count(*) AS qty FROM %2$s GROUP BY %1$I HAVING count(*) > 1 ORDER BY %1$I', key_col, qualified) LOOP
        valid := false;
        RAISE NOTICE 'FAIL duplicate ID % in % actual count % expected 1', duplicate.id, qualified, duplicate.qty;
    END LOOP;
    IF NOT valid THEN RAISE EXCEPTION 'FAIL duplicate IDs found in %', qualified; END IF;
    RAISE NOTICE 'PASS duplicate IDs % expected 0 actual 0', qualified;

    IF profile IN ('fides','multi','tpch') THEN
        FOR details IN EXECUTE format(
            'SELECT split_part(i.en_ind,''|'',1) AS id, split_part(i.en_ind,''|'',2) AS operation,
             length(i.ut) AS ut_length, length(i.join_col) AS auxiliary_length,
             EXISTS (SELECT 1 FROM %s d WHERE d.%I = split_part(i.en_ind,''|'',1)) AS data_exists
             FROM %I.%I i ORDER BY i.en_ind LIMIT 20', qualified, key_col, ns, tables[2]) LOOP
            RAISE NOTICE 'INDEX id % operation % UT length % auxiliary length % data exists %',
                details.id, details.operation, details.ut_length, details.auxiliary_length, details.data_exists;
        END LOOP;
    END IF;
END $$;
COMMIT;
