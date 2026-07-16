DROP FUNCTION IF EXISTS sse_order_create_tables(TEXT, TEXT, TEXT, TEXT, TEXT, TEXT, TEXT, TEXT, TEXT, TEXT);
CREATE OR REPLACE FUNCTION sse_order_create_tables(table_name TEXT, O_ORDERKEY TEXT, O_CUSTKEY TEXT, O_ORDERSTATUS TEXT, O_TOTALPRICE TEXT, O_ORDERDATE TEXT, O_ORDERPRIORITY TEXT, O_CLERK TEXT, O_SHIPPRIORITY TEXT, O_COMMENT TEXT)
RETURNS VOID AS $$
DECLARE
    data_table_name TEXT := table_name || '_data';  -- 拼接data表名
    index_table_name TEXT := table_name || '_index';  -- 拼接index表名
    ut TEXT := 'ut';  -- 默认值
    en_ind TEXT := 'en_ind';
    join_col TEXT := 'join_col';
BEGIN
    -- 创建表data
    EXECUTE format('
        CREATE TABLE IF NOT EXISTS %I (
            %I TEXT, %I TEXT, %I TEXT, %I TEXT, %I TEXT, %I TEXT, %I TEXT, %I TEXT, %I TEXT
        )', lower(data_table_name), lower(O_ORDERKEY), lower(O_CUSTKEY), lower(O_ORDERSTATUS), lower(O_TOTALPRICE), lower(O_ORDERDATE), lower(O_ORDERPRIORITY), lower(O_CLERK), lower(O_SHIPPRIORITY), lower(O_COMMENT));

    -- 创建表index
    EXECUTE format('
        CREATE TABLE IF NOT EXISTS %I (
            %I TEXT,
            %I TEXT,
            %I TEXT
        )', lower(index_table_name), lower(ut), lower(en_ind), lower(join_col));

END;
$$ LANGUAGE plpgsql;



DROP FUNCTION IF EXISTS fides_tpch_insert(TEXT[], TEXT[], TEXT[], TEXT[], TEXT[], TEXT[], TEXT[], TEXT[], TEXT[], TEXT[], TEXT[], TEXT);
CREATE OR REPLACE FUNCTION fides_tpch_insert(O_ORDERKEY text[], O_CUSTKEY text[], O_ORDERSTATUS text[], O_TOTALPRICE text[], O_ORDERDATE text[], O_ORDERPRIORITY text[], O_CLERK text[], O_SHIPPRIORITY text[], O_COMMENT text[], 
    p_ut text[],
    p_join text[],
    table_prefix TEXT      -- 表名前缀
) RETURNS VOID AS $$
DECLARE
    data_table_name TEXT := table_prefix || '_data';  -- 拼接data表名
    index_table_name TEXT := table_prefix || '_index';  -- 拼接index表名
    sql_query1 TEXT;
    sql_query2 TEXT;
BEGIN
    -- 构建插入语句
    sql_query1 := format('INSERT INTO %I (O_ORDERKEY, O_CUSTKEY, O_ORDERSTATUS, O_TOTALPRICE, O_ORDERDATE, O_ORDERPRIORITY, O_CLERK, O_SHIPPRIORITY, O_COMMENT) VALUES ', data_table_name);
    sql_query2 := format('INSERT INTO %I (UT, en_ind, join_col) VALUES ', index_table_name);
    
    FOR i IN 1..array_length(O_ORDERKEY, 1) LOOP
        sql_query1 := sql_query1 || '(' || quote_literal(O_ORDERKEY[i]) || ', ' || quote_literal(O_CUSTKEY[i]) || ', ' || quote_literal(O_ORDERSTATUS[i]) || ', ' || quote_literal(O_TOTALPRICE[i]) || ', ' || quote_literal(O_ORDERDATE[i]) || ', ' || quote_literal(O_ORDERPRIORITY[i]) || ', ' || quote_literal(O_CLERK[i]) || ', ' || quote_literal(O_SHIPPRIORITY[i]) || ', ' || quote_literal(O_COMMENT[i]) || '),';
        sql_query2 := sql_query2 || '(' || quote_literal(p_ut[i]) || ', ' || quote_literal(O_ORDERKEY[i] || '|add') || ', ' || quote_literal(p_join[i]) || '),';
    END LOOP;
    
    -- 去除最后一个逗号
    sql_query1 := left(sql_query1, length(sql_query1) - 1);
    sql_query2 := left(sql_query2, length(sql_query2) - 1);
    
    -- 执行插入语句
    EXECUTE sql_query1;
    EXECUTE sql_query2;
END;
$$ LANGUAGE plpgsql;


DROP FUNCTION IF EXISTS fides_tpch_select(VARCHAR[], VARCHAR[], VARCHAR[], TEXT);
CREATE OR REPLACE FUNCTION fides_tpch_select(
    p_ut VARCHAR[], 
    p_ind VARCHAR[], 
    p_join VARCHAR[], 
    table_prefix TEXT      -- 表名前缀
) RETURNS TABLE(O_ORDERKEY TEXT, O_CUSTKEY TEXT, O_ORDERSTATUS TEXT, O_TOTALPRICE TEXT, O_ORDERDATE TEXT, O_ORDERPRIORITY TEXT, O_CLERK TEXT, O_SHIPPRIORITY TEXT, O_COMMENT TEXT) AS $$
DECLARE
    insert_sql TEXT;
    search_sql TEXT;
    data_table_name TEXT := table_prefix || '_data';  -- 拼接data表名
    index_table_name TEXT := table_prefix || '_index';  -- 拼接index表名
BEGIN
    -- 构建插入数据索引的动态 SQL 语句
    insert_sql := format('INSERT INTO %I (UT, en_ind, join_col) VALUES ', index_table_name);
    FOR i IN 1..array_length(p_ind, 1) LOOP
        insert_sql := insert_sql || '(' || quote_literal(p_ut[i]) || ', ' || quote_literal(p_ind[i] || '|add') || ', ' || quote_literal(p_join[i] ) || '),';
    END LOOP;
    insert_sql := rtrim(insert_sql, ',');

    -- 执行插入操作
    EXECUTE insert_sql;

    -- 构建搜索数据的动态 SQL 语句
    search_sql := format('SELECT * FROM %I WHERE O_ORDERKEY IN (', data_table_name);
    FOR i IN 1..array_length(p_ind, 1) LOOP
        search_sql := search_sql || quote_literal(p_ind[i]) || ', ';
    END LOOP;
    search_sql := rtrim(search_sql, ', ');  -- 去掉最后一个逗号和空格
    search_sql := search_sql || ')';

    -- 执行搜索操作并将结果返回
    RETURN QUERY EXECUTE search_sql;
END;
$$ LANGUAGE plpgsql;