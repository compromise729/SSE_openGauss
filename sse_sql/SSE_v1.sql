-- SHY-02 v1.0. Load in the application database with ON_ERROR_STOP=1.
BEGIN;

CREATE TABLE public.sse_v1_catalog (
    logical_schema text NOT NULL CHECK (logical_schema = 'public'),
    logical_table text NOT NULL CHECK (logical_table ~ '^[a-z][a-z0-9_]{0,39}$'),
    protocol_version integer NOT NULL CHECK (protocol_version = 1),
    PRIMARY KEY (logical_schema, logical_table)
);

CREATE FUNCTION public.sse_v1_check_name(schema_name text, logical_name text)
RETURNS void LANGUAGE plpgsql IMMUTABLE AS $$
BEGIN
    IF schema_name IS DISTINCT FROM 'public' OR logical_name IS NULL
       OR logical_name !~ '^[a-z][a-z0-9_]{0,39}$' THEN
        RAISE EXCEPTION 'invalid sse_v1 table identifier' USING ERRCODE = '22023';
    END IF;
END;
$$;

CREATE FUNCTION public.sse_v1_blob_valid(blob text, purpose integer)
RETURNS boolean LANGUAGE plpgsql IMMUTABLE STRICT AS $$
DECLARE
    raw bytea;
    raw_size integer;
    declared_size bigint;
BEGIN
    IF (purpose = 1 AND octet_length(blob) NOT BETWEEN 88 AND 65624)
       OR (purpose = 2 AND octet_length(blob) <> 96) OR purpose NOT IN (1, 2) THEN
        RETURN false;
    END IF;
    IF octet_length(blob) % 4 <> 0 OR blob !~ '^[A-Za-z0-9+/]+={0,2}$' THEN RETURN false; END IF;
    raw := pg_catalog.decode(blob, 'base64');
    raw_size := octet_length(raw);
    IF raw_size < 54 OR get_byte(raw, 0) <> 83 OR get_byte(raw, 1) <> 83
       OR get_byte(raw, 2) <> 69 OR get_byte(raw, 3) <> 49
       OR get_byte(raw, 4) <> 1 OR get_byte(raw, 5) <> purpose THEN RETURN false; END IF;
    declared_size := get_byte(raw, 34)::bigint * 16777216 + get_byte(raw, 35) * 65536
                   + get_byte(raw, 36) * 256 + get_byte(raw, 37);
    RETURN declared_size = raw_size - 54
       AND ((purpose = 1 AND declared_size BETWEEN 12 AND 49164) OR (purpose = 2 AND declared_size = 17))
       AND replace(pg_catalog.encode(raw, 'base64'), chr(10), '') = blob;
EXCEPTION WHEN invalid_parameter_value OR invalid_text_representation THEN
    RETURN false;
END;
$$;

CREATE FUNCTION public.sse_v1_create(schema_name text, logical_name text)
RETURNS void LANGUAGE plpgsql AS $$
BEGIN
    PERFORM public.sse_v1_check_name(schema_name, logical_name);
    IF EXISTS (
        SELECT 1 FROM pg_catalog.pg_class c JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace
        WHERE n.nspname = schema_name AND c.relname IN (logical_name, logical_name || '_data', logical_name || '_index')
    ) THEN
        RAISE EXCEPTION 'sse_v1 table name is already in use: %', logical_name USING ERRCODE = '42P07';
    END IF;
    EXECUTE format('CREATE TABLE %I.%I (
        rid text COLLATE "C" PRIMARY KEY CHECK (rid ~ ''^[0-9a-f]{32}$''),
        data_blob text NOT NULL CHECK (public.sse_v1_blob_valid(data_blob, 1)))', schema_name, logical_name || '_data');
    EXECUTE format('CREATE TABLE %I.%I (
        ut text COLLATE "C" PRIMARY KEY CHECK (ut ~ ''^[A-Za-z0-9+/]{22}==$''),
        locator_blob text NOT NULL CHECK (public.sse_v1_blob_valid(locator_blob, 2)))', schema_name, logical_name || '_index');
    INSERT INTO public.sse_v1_catalog VALUES (schema_name, logical_name, 1);
END;
$$;

CREATE FUNCTION public.sse_v1_require_table(schema_name text, logical_name text)
RETURNS void LANGUAGE plpgsql STABLE AS $$
DECLARE
    data_columns text;
    index_columns text;
BEGIN
    PERFORM public.sse_v1_check_name(schema_name, logical_name);
    IF NOT EXISTS (SELECT 1 FROM public.sse_v1_catalog c WHERE c.logical_schema = schema_name
                   AND c.logical_table = logical_name AND c.protocol_version = 1) THEN
        RAISE EXCEPTION 'unregistered sse_v1 table: %', logical_name USING ERRCODE = '42P01';
    END IF;
    SELECT string_agg(CASE WHEN c.relname = logical_name || '_data'
                           THEN a.attname || ':' || format_type(a.atttypid, a.atttypmod) END, ',' ORDER BY a.attnum),
           string_agg(CASE WHEN c.relname = logical_name || '_index'
                           THEN a.attname || ':' || format_type(a.atttypid, a.atttypmod) END, ',' ORDER BY a.attnum)
    INTO data_columns, index_columns
    FROM pg_catalog.pg_namespace n JOIN pg_catalog.pg_class c ON c.relnamespace = n.oid
    JOIN pg_catalog.pg_attribute a ON a.attrelid = c.oid
    WHERE n.nspname = schema_name AND c.relkind = 'r' AND NOT a.attisdropped AND a.attnum > 0
          AND c.relname IN (logical_name || '_data', logical_name || '_index');
    IF data_columns IS DISTINCT FROM 'rid:text,data_blob:text'
       OR index_columns IS DISTINCT FROM 'ut:text,locator_blob:text' THEN
        RAISE EXCEPTION 'invalid sse_v1 table shape: %', logical_name USING ERRCODE = '42P01';
    END IF;
END;
$$;

CREATE FUNCTION public.sse_v1_array_size(items text[]) RETURNS integer
LANGUAGE plpgsql IMMUTABLE AS $$
BEGIN
    IF items IS NULL OR COALESCE(array_ndims(items), 1) <> 1 OR COALESCE(array_lower(items, 1), 1) <> 1 THEN
        RAISE EXCEPTION 'expected non-null one-dimensional array with lower bound 1' USING ERRCODE = '22023';
    END IF;
    RETURN COALESCE(array_length(items, 1), 0);
END;
$$;

CREATE FUNCTION public.sse_v1_insert(schema_name text, logical_name text, rids text[],
                                    data_blobs text[], uts text[], locator_blobs text[])
RETURNS void LANGUAGE plpgsql AS $$
DECLARE
    batch_size integer := public.sse_v1_array_size(rids);
BEGIN
    PERFORM public.sse_v1_require_table(schema_name, logical_name);
    IF batch_size NOT BETWEEN 1 AND 100 OR public.sse_v1_array_size(data_blobs) <> batch_size
       OR public.sse_v1_array_size(uts) <> batch_size OR public.sse_v1_array_size(locator_blobs) <> batch_size THEN
        RAISE EXCEPTION 'sse_v1 insert requires 1 to 100 equally sized rows' USING ERRCODE = '22023';
    END IF;
    EXECUTE format('INSERT INTO %I.%I (rid, data_blob)
        SELECT $1[s], $2[s] FROM generate_subscripts($1, 1) AS g(s)', schema_name, logical_name || '_data')
        USING rids, data_blobs;
    PERFORM public.sse_v1_reinsert(schema_name, logical_name, uts, locator_blobs);
END;
$$;

CREATE FUNCTION public.sse_v1_reinsert(schema_name text, logical_name text, uts text[], locator_blobs text[])
RETURNS void LANGUAGE plpgsql AS $$
DECLARE
    batch_size integer := public.sse_v1_array_size(uts);
BEGIN
    PERFORM public.sse_v1_require_table(schema_name, logical_name);
    IF public.sse_v1_array_size(locator_blobs) <> batch_size THEN
        RAISE EXCEPTION 'sse_v1 reinsert array length mismatch' USING ERRCODE = '22023';
    END IF;
    IF batch_size = 0 THEN RETURN; END IF;
    EXECUTE format('INSERT INTO %I.%I (ut, locator_blob)
        SELECT $1[s], $2[s] FROM generate_subscripts($1, 1) AS g(s)', schema_name, logical_name || '_index')
        USING uts, locator_blobs;
END;
$$;

-- Backend-only adapter for the derived updates from the Fides request.
CREATE FUNCTION public.sse_v1_take(schema_name text, logical_name text, uts text[])
RETURNS TABLE (ut text, locator_blob text) LANGUAGE plpgsql AS $$
BEGIN
    PERFORM public.sse_v1_require_table(schema_name, logical_name);
    IF public.sse_v1_array_size(uts) = 0 THEN RETURN; END IF;
    RETURN QUERY EXECUTE format('DELETE FROM %I.%I AS i
        WHERE i.ut = ANY($1) RETURNING i.ut, i.locator_blob', schema_name, logical_name || '_index') USING uts;
END;
$$;

CREATE FUNCTION public.sse_v1_fetch(schema_name text, logical_name text, rids text[])
RETURNS TABLE (rid text, data_blob text) LANGUAGE plpgsql STABLE AS $$
DECLARE
    wanted integer := public.sse_v1_array_size(rids);
    found_rows integer;
BEGIN
    PERFORM public.sse_v1_require_table(schema_name, logical_name);
    IF wanted = 0 THEN RETURN; END IF;
    IF EXISTS (SELECT 1 FROM unnest(rids) AS ids(value) WHERE value IS NULL OR value !~ '^[0-9a-f]{32}$')
       OR (SELECT count(DISTINCT value COLLATE "C") FROM unnest(rids) AS ids(value)) <> wanted THEN
        RAISE EXCEPTION 'sse_v1 inconsistent_result: invalid or repeated requested rid' USING ERRCODE = '21000';
    END IF;
    RETURN QUERY EXECUTE format('SELECT d.rid, d.data_blob
        FROM generate_subscripts($1, 1) AS g(s) JOIN %I.%I AS d ON d.rid = $1[s]
        ORDER BY s', schema_name, logical_name || '_data') USING rids;
    GET DIAGNOSTICS found_rows = ROW_COUNT;
    IF found_rows <> wanted THEN
        RAISE EXCEPTION 'sse_v1 inconsistent_result: requested %, found %', wanted, found_rows USING ERRCODE = '21000';
    END IF;
END;
$$;
COMMIT;
