-- Read-only inspection after the real YZH-01/Fides round trip.
\set ON_ERROR_STOP on
BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ READ ONLY;

SELECT logical_schema, logical_table, protocol_version
FROM public.sse_v1_catalog WHERE logical_schema = 'public' AND logical_table = 'week_demo';

SELECT rid, octet_length(data_blob) AS data_bytes, md5(data_blob) AS data_digest
FROM public.week_demo_data ORDER BY rid;

SELECT ut, octet_length(locator_blob) AS locator_bytes, md5(locator_blob) AS locator_digest
FROM public.week_demo_index ORDER BY ut;

-- The client supplies only the authenticated rid values. This reproduces
-- the fetch shape with one fixture rid selected for this read-only audit.
SELECT rid, octet_length(data_blob) AS data_bytes, md5(data_blob) AS data_digest
FROM public.sse_v1_fetch('public', 'week_demo', ARRAY[
    (SELECT rid FROM public.week_demo_data ORDER BY octet_length(data_blob) DESC, rid LIMIT 1)
]);

COMMIT;
