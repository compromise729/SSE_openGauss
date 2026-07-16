CREATE TABLE fides_tpch (O_ORDERKEY text, O_CUSTKEY text SSE='Fides', O_ORDERSTATUS text, O_TOTALPRICE text, O_ORDERDATE text, O_ORDERPRIORITY text, O_CLERK text, O_SHIPPRIORITY text, O_COMMENT text);  
\i /opt/og/openGauss-server/sse_sql/SSE_init.sql
\timing     
\i /opt/og/openGauss-server/sse_sql/fides_insert_100-10000.sql
\i /opt/og/openGauss-server/sse_sql/fides_insert_100-1000.sql
SELECT * from fides_tpch where keyword='136237';
SELECT * from fides_tpch where keyword='75011';
SELECT * from fides_tpch where keyword='10';
DELETE from fides_tpch where ind='3970' and keyword='75011';
SELECT * from fides_tpch where keyword='75011';
SELECT pg_size_pretty(pg_total_relation_size('fides_tpch_data')) AS total_size;
SELECT pg_size_pretty(pg_total_relation_size('fides_tpch_index')) AS total_size;

CREATE TABLE mitra_tpch (O_ORDERKEY text, O_CUSTKEY text SSE='Mitra', O_ORDERSTATUS text, O_TOTALPRICE text, O_ORDERDATE text, O_ORDERPRIORITY text, O_CLERK text, O_SHIPPRIORITY text, O_COMMENT text);
\i /opt/og/openGauss-server/sse_sql/SSE_init.sql
\timing
\i /opt/og/openGauss-server/sse_sql/mitra_insert_20-5000.sql
\i /opt/og/openGauss-server/sse_sql/mitra_insert_100-1000.sql
SELECT * from mitra_tpch where keyword='136237';
SELECT * from mitra_tpch where keyword='75011';
SELECT * from mitra_tpch where keyword='10';
DELETE from mitra_tpch where ind='1380' and keyword='136237';
SELECT * from mitra_tpch where keyword='136237';
SELECT pg_size_pretty(pg_total_relation_size('mitra_tpch_data')) AS total_size;
SELECT pg_size_pretty(pg_total_relation_size('mitra_tpch_index')) AS total_size;

CREATE TABLE fbdsse_tpch (O_ORDERKEY text, O_CUSTKEY text SSE='FBDSSE', O_ORDERSTATUS text, O_TOTALPRICE text, O_ORDERDATE text, O_ORDERPRIORITY text, O_CLERK text, O_SHIPPRIORITY text, O_COMMENT text);
\i /opt/og/openGauss-server/sse_sql/SSE_init.sql  
\i /opt/og/openGauss-server/sse_sql/insert_1-200.sql
\timing
\i /opt/og/openGauss-server/sse_sql/fbdsse_insert_10-1000-1.sql
\i /opt/og/openGauss-server/sse_sql/fbdsse_insert_10-1000-2.sql
\i /opt/og/openGauss-server/sse_sql/fbdsse_insert_10-1000-3.sql
\i /opt/og/openGauss-server/sse_sql/fbdsse_insert_10-1000-4.sql
\i /opt/og/openGauss-server/sse_sql/fbdsse_insert_10-1000-5.sql
\i /opt/og/openGauss-server/sse_sql/fbdsse_insert_10-1000-6.sql
\i /opt/og/openGauss-server/sse_sql/fbdsse_insert_10-1000-7.sql
\i /opt/og/openGauss-server/sse_sql/fbdsse_insert_10-1000-8.sql
\i /opt/og/openGauss-server/sse_sql/fbdsse_insert_10-1000-9.sql
\i /opt/og/openGauss-server/sse_sql/fbdsse_insert_10-1000-10.sql
SELECT * from fbdsse_tpch where keyword='136237';
SELECT * from fbdsse_tpch where keyword='75011';
SELECT * from fbdsse_tpch where keyword='10';
DELETE from fbdsse_tpch where ind='1380' and keyword='136237';
SELECT * from fbdsse_tpch where keyword='136237';
SELECT pg_size_pretty(pg_total_relation_size('fbdsse_tpch_data')) AS total_size;
SELECT pg_size_pretty(pg_total_relation_size('fbdsse_tpch_index')) AS total_size;

CREATE TABLE orion_tpch (O_ORDERKEY text, O_CUSTKEY text SSE='ORION', O_ORDERSTATUS text, O_TOTALPRICE text, O_ORDERDATE text, O_ORDERPRIORITY text, O_CLERK text, O_SHIPPRIORITY text, O_COMMENT text);
-- 若orion_state.bin无数据需先删除该文件
orion_setup;
\i /opt/og/openGauss-server/sse_sql/orion_insert_100-1000.sql
-- \i /opt/og/openGauss-server/sse_sql/orion_insert_1000-100.sql
\i /opt/og/openGauss-server/sse_sql/insert_1-200.sql
SELECT * from orion_tpch where keyword='136237';
SELECT * from orion_tpch where keyword='75011';
SELECT * from orion_tpch where keyword='10';
DELETE from orion_tpch where ind='1380' and keyword='136237';
SELECT * from orion_tpch where keyword='136237';
SELECT pg_size_pretty(pg_total_relation_size('orion_tpch_data')) AS total_size;
-- SELECT pg_size_pretty(pg_relation_size('orion_tpch_data')) AS table_size;

CREATE TABLE closefb_tpch (O_ORDERKEY text, O_CUSTKEY text SSE='CloseFB', O_ORDERSTATUS text, O_TOTALPRICE text, O_ORDERDATE text, O_ORDERPRIORITY text, O_CLERK text, O_SHIPPRIORITY text, O_COMMENT text);
./sse_sql/closefb_setup.sh closefb_tpch
\timing
\i /opt/og/openGauss-server/sse_sql/closefb_insert_100-1000.sql
SELECT * from closefb_tpch where keyword='136237';
SELECT * from closefb_tpch where keyword='75011';
SELECT * from closefb_tpch where keyword='10';
-- delete from closefb_tpch values (ind, keyword),...
delete from closefb_tpch values ('1380', '136237');
SELECT * from closefb_tpch where keyword='136237';
SELECT pg_size_pretty(pg_total_relation_size('closefb_tpch_data')) AS total_size;
SELECT pg_size_pretty(pg_total_relation_size('closefb_tpch_index')) AS total_size;

CREATE TABLE dual_tpch (O_ORDERKEY text, O_CUSTKEY text SSE='DUAL', O_ORDERSTATUS text, O_TOTALPRICE text, O_ORDERDATE text, O_ORDERPRIORITY text, O_CLERK text, O_SHIPPRIORITY text, O_COMMENT text);
-- ./sse_sql/closefb_setup.sh closefb_tpch
\timing
\i /opt/og/openGauss-server/sse_sql/SSE_init.sql
\i /opt/og/openGauss-server/sse_sql/dual_insert_100-1000.sql
SELECT * from dual_tpch where keyword='136237';
SELECT * from dual_tpch where keyword='75011';
SELECT * from dual_tpch where keyword='10';
-- DELETE from dual_tpch where ind='3970' and keyword='75011';
DELETE from dual_tpch where ind='1380';
SELECT * from dual_tpch where keyword='136237';
SELECT pg_size_pretty(pg_total_relation_size('dual_tpch_data')) AS total_size;
SELECT pg_size_pretty(pg_total_relation_size('dual_tpch_index')) AS total_size;

CREATE TABLE januspp_tpch (O_ORDERKEY text, O_CUSTKEY text SSE='JANUSPP', O_ORDERSTATUS text, O_TOTALPRICE text, O_ORDERDATE text, O_ORDERPRIORITY text, O_CLERK text, O_SHIPPRIORITY text, O_COMMENT text);
-- ./sse_sql/closefb_setup.sh closefb_tpch
-- ./sse_sql/januspp_clear.sh januspp_tpch
\timing
\i /opt/og/openGauss-server/sse_sql/januspp_insert_100-1000.sql
SELECT * from januspp_tpch where keyword='136237';
SELECT * from januspp_tpch where keyword='75011';
SELECT * from januspp_tpch where keyword='10';
delete from januspp_tpch where ind='1380' and keyword='136237';
SELECT * from januspp_tpch where keyword='136237';
SELECT pg_size_pretty(pg_total_relation_size('januspp_tpch_data')) AS total_size;
SELECT pg_size_pretty(pg_total_relation_size('januspp_tpch_indexadd')) AS total_size;
SELECT pg_size_pretty(pg_total_relation_size('januspp_tpch_indexdel')) AS total_size;
SELECT pg_size_pretty(pg_total_relation_size('januspp_tpch_cache')) AS total_size;