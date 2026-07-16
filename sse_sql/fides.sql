DROP FUNCTION IF EXISTS sse_create_tables(TEXT, TEXT, TEXT, TEXT);
CREATE OR REPLACE FUNCTION sse_create_tables(table_name TEXT, ind TEXT, keyword TEXT, value_col TEXT)
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
            %I TEXT,
            %I TEXT,
            %I TEXT
        )', lower(data_table_name), lower(ind), lower(keyword), lower(value_col));

    -- 创建表index
    EXECUTE format('
        CREATE TABLE IF NOT EXISTS %I (
            %I TEXT,
            %I TEXT,
            %I TEXT
        )', lower(index_table_name), lower(ut), lower(en_ind), lower(join_col));

    -- RAISE NOTICE 'Tables data and index created with columns: %, %, % and %, %, %', 
    --     ind, keyword, value_col, ut, en_ind, join_col;
END;
$$ LANGUAGE plpgsql;

DROP FUNCTION IF EXISTS fbdsse_create_tables(TEXT, TEXT, TEXT, TEXT);


-- DROP FUNCTION IF EXISTS fides_insert_bulk(TEXT[], TEXT[], TEXT[], TEXT[]);
-- CREATE OR REPLACE FUNCTION fides_insert_bulk(p_ind text[], p_keyword text[], p_value text[], p_ut text[]) RETURNS VOID AS $$
-- DECLARE
--     sql_query1 TEXT;
--     sql_query2 TEXT;
-- BEGIN
--     -- 构建插入语句
--     sql_query1 := 'INSERT INTO fides_datatable (ind, keyword, values) VALUES ';
--     sql_query2 := 'INSERT INTO fides_indextable (UT, en_indop) VALUES ';
--     FOR i IN 1..array_length(p_ind, 1) LOOP
--         sql_query1 := sql_query1 || '(' || quote_literal(p_ind[i]) || ', ' || quote_literal(p_keyword[i]) || ', ' || quote_literal(p_value[i]) || '),';
--         sql_query2 := sql_query2 || '(' || quote_literal(p_ut[i]) || ', ' || quote_literal(p_ind[i] || '|add') || '),';
--     END LOOP;
--     -- 去除最后一个逗号
--     sql_query1 := left(sql_query1, length(sql_query1) - 1);
--     sql_query2 := left(sql_query2, length(sql_query2) - 1);
--     -- 执行插入语句
--     EXECUTE sql_query1;
--     EXECUTE sql_query2;
-- END;
-- $$ LANGUAGE plpgsql;

-- DROP FUNCTION IF EXISTS fides_insert_bulk(TEXT[], TEXT[], TEXT[], TEXT[], TEXT);
-- CREATE OR REPLACE FUNCTION fides_insert_bulk(
--     p_ind text[], 
--     p_keyword text[], 
--     p_value text[], 
--     p_ut text[],
--     table_prefix TEXT      -- 表名前缀
-- ) RETURNS VOID AS $$
-- DECLARE
--     data_table_name TEXT := table_prefix || '_data';  -- 拼接data表名
--     index_table_name TEXT := table_prefix || '_index';  -- 拼接index表名
--     sql_query1 TEXT;
--     sql_query2 TEXT;
-- BEGIN
--     -- 构建插入语句
--     sql_query1 := format('INSERT INTO %I (ind, keyword, values) VALUES ', data_table_name);
--     sql_query2 := format('INSERT INTO %I (UT, en_indop) VALUES ', index_table_name);
    
--     FOR i IN 1..array_length(p_ind, 1) LOOP
--         sql_query1 := sql_query1 || '(' || quote_literal(p_ind[i]) || ', ' || quote_literal(p_keyword[i]) || ', ' || quote_literal(p_value[i]) || '),';
--         sql_query2 := sql_query2 || '(' || quote_literal(p_ut[i]) || ', ' || quote_literal(p_ind[i] || '|add') || '),';
--     END LOOP;
    
--     -- 去除最后一个逗号
--     sql_query1 := left(sql_query1, length(sql_query1) - 1);
--     sql_query2 := left(sql_query2, length(sql_query2) - 1);
    
--     -- 执行插入语句
--     EXECUTE sql_query1;
--     EXECUTE sql_query2;
-- END;
-- $$ LANGUAGE plpgsql;


DROP FUNCTION IF EXISTS fides_insert_bulk(TEXT[], TEXT[], TEXT[], TEXT[], TEXT[], TEXT);
CREATE OR REPLACE FUNCTION fides_insert_bulk(
    p_ind text[], 
    p_keyword text[], 
    p_value text[], 
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
    sql_query1 := format('INSERT INTO %I (ind, keyword, values) VALUES ', data_table_name);
    sql_query2 := format('INSERT INTO %I (UT, en_ind, join_col) VALUES ', index_table_name);
    
    FOR i IN 1..array_length(p_ind, 1) LOOP
        sql_query1 := sql_query1 || '(' || quote_literal(p_ind[i]) || ', ' || quote_literal(p_keyword[i]) || ', ' || quote_literal(p_value[i]) || '),';
        sql_query2 := sql_query2 || '(' || quote_literal(p_ut[i]) || ', ' || quote_literal(p_ind[i] || '|add') || ', ' || quote_literal(p_join[i]) || '),';
    END LOOP;
    
    -- 去除最后一个逗号
    sql_query1 := left(sql_query1, length(sql_query1) - 1);
    sql_query2 := left(sql_query2, length(sql_query2) - 1);
    
    -- 执行插入语句
    EXECUTE sql_query1;
    EXECUTE sql_query2;
END;
$$ LANGUAGE plpgsql;



-- CREATE TABLE Diana_datatable (ind text, keyword text, values text);
-- CREATE TABLE Diana_indextable (UT text, ind text);
DROP FUNCTION IF EXISTS diana_insert_bulk(TEXT[], TEXT[], TEXT[], TEXT[], TEXT[]);
CREATE OR REPLACE FUNCTION diana_insert_bulk(p_ind text[], p_keyword text[], p_value text[], p_ut text[], p_tokenind text[]) RETURNS VOID AS $$
DECLARE
    sql_query1 TEXT;
    sql_query2 TEXT;
BEGIN
    -- 构建插入语句
    sql_query1 := 'INSERT INTO diana_datatable (ind, keyword, values) VALUES ';
    sql_query2 := 'INSERT INTO diana_indextable (UT, ind) VALUES ';
    FOR i IN 1..array_length(p_ind, 1) LOOP
        sql_query1 := sql_query1 || '(' || quote_literal(p_ind[i]) || ', ' || quote_literal(p_keyword[i]) || ', ' || quote_literal(p_value[i]) || '),';
        sql_query2 := sql_query2 || '(' || quote_literal(p_ut[i]) || ', ' || quote_literal(p_tokenind[i]) || '),';
    END LOOP;
    -- 去除最后一个逗号
    sql_query1 := left(sql_query1, length(sql_query1) - 1);
    sql_query2 := left(sql_query2, length(sql_query2) - 1);
    -- 执行插入语句
    EXECUTE sql_query1;
    EXECUTE sql_query2;
END;
$$ LANGUAGE plpgsql;







-- DROP FUNCTION IF EXISTS fides_select(VARCHAR[], VARCHAR[]);
-- CREATE OR REPLACE FUNCTION fides_select(p_ut VARCHAR[], p_ind VARCHAR[])
-- RETURNS TABLE(ind TEXT, keyword TEXT, context TEXT) AS $$
-- DECLARE
--     insert_sql TEXT;
--     search_sql TEXT;
-- BEGIN
--     -- 构建插入数据索引的动态 SQL 语句
--     insert_sql := 'INSERT INTO fides_indextable (UT, en_indop) VALUES ';
--     FOR i IN 1..array_length(p_ind, 1) LOOP
--         insert_sql := insert_sql || '(' || quote_literal(p_ut[i]) || ', ' || quote_literal(p_ind[i] || '|add') || '),';
--     END LOOP;
--     insert_sql := rtrim(insert_sql, ',');
    
--     -- 执行插入操作
--     EXECUTE insert_sql;
    
--     -- 构建搜索数据的动态 SQL 语句
--     search_sql := 'SELECT * FROM fides_datatable WHERE ind IN (';
--     FOR i IN 1..array_length(p_ind, 1) LOOP
--         search_sql := search_sql || quote_literal(p_ind[i]) || ', ';
--     END LOOP;
--     search_sql := rtrim(search_sql, ', ');--逗号后面有个空格
--     search_sql := search_sql || ')';
--     -- 执行搜索操作并将结果返回
--     RETURN QUERY EXECUTE search_sql;
-- END;
-- $$ LANGUAGE plpgsql;



-- DROP FUNCTION IF EXISTS fides_select(VARCHAR[], VARCHAR[], TEXT);
-- CREATE OR REPLACE FUNCTION fides_select(
--     p_ut VARCHAR[], 
--     p_ind VARCHAR[], 
--     table_prefix TEXT      -- 表名前缀
-- ) RETURNS TABLE(ind TEXT, keyword TEXT, context TEXT) AS $$
-- DECLARE
--     insert_sql TEXT;
--     search_sql TEXT;
--     data_table_name TEXT := table_prefix || '_data';  -- 拼接data表名
--     index_table_name TEXT := table_prefix || '_index';  -- 拼接index表名
-- BEGIN
--     -- 构建插入数据索引的动态 SQL 语句
--     insert_sql := format('INSERT INTO %I (UT, en_indop) VALUES ', index_table_name);
--     FOR i IN 1..array_length(p_ind, 1) LOOP
--         insert_sql := insert_sql || '(' || quote_literal(p_ut[i]) || ', ' || quote_literal(p_ind[i] || '|add') || '),';
--     END LOOP;
--     insert_sql := rtrim(insert_sql, ',');

--     -- 执行插入操作
--     EXECUTE insert_sql;

--     -- 构建搜索数据的动态 SQL 语句
--     search_sql := format('SELECT * FROM %I WHERE ind IN (', data_table_name);
--     FOR i IN 1..array_length(p_ind, 1) LOOP
--         search_sql := search_sql || quote_literal(p_ind[i]) || ', ';
--     END LOOP;
--     search_sql := rtrim(search_sql, ', ');  -- 去掉最后一个逗号和空格
--     search_sql := search_sql || ')';

--     -- 执行搜索操作并将结果返回
--     RETURN QUERY EXECUTE search_sql;
-- END;
-- $$ LANGUAGE plpgsql;



DROP FUNCTION IF EXISTS fides_select(VARCHAR[], VARCHAR[], VARCHAR[], TEXT);
CREATE OR REPLACE FUNCTION fides_select(
    p_ut VARCHAR[], 
    p_ind VARCHAR[], 
    p_join VARCHAR[], 
    table_prefix TEXT      -- 表名前缀
) RETURNS TABLE(ind TEXT, keyword TEXT, context TEXT) AS $$
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
    search_sql := format('SELECT * FROM %I WHERE ind IN (', data_table_name);
    FOR i IN 1..array_length(p_ind, 1) LOOP
        search_sql := search_sql || quote_literal(p_ind[i]) || ', ';
    END LOOP;
    search_sql := rtrim(search_sql, ', ');  -- 去掉最后一个逗号和空格
    search_sql := search_sql || ')';

    -- 执行搜索操作并将结果返回
    RETURN QUERY EXECUTE search_sql;
END;
$$ LANGUAGE plpgsql;

-- DROP FUNCTION IF EXISTS fides_join(TEXT , TEXT);
-- CREATE OR REPLACE FUNCTION fides_join(
--     insert_sql1 text,
--     insert_sql2 text
--     -- select_sql text
-- ) RETURNS void AS $$
-- BEGIN
--     -- 执行第一个 INSERT 语句
--     EXECUTE insert_sql1;

--     -- 执行第二个 INSERT 语句
--     EXECUTE insert_sql2;

--     -- 执行 SELECT 语句
--     -- EXECUTE select_sql;
-- END;
-- $$ LANGUAGE plpgsql;

DROP FUNCTION IF EXISTS fides_join(TEXT , TEXT, TEXT);
CREATE OR REPLACE FUNCTION fides_join(
    insert_sql1 text,
    insert_sql2 text,
    select_sql text
) RETURNS TABLE (t1_ind TEXT, t1_keyword TEXT, t1_context TEXT, t2_ind TEXT, t2_keyword TEXT, t2_context TEXT) AS $$
BEGIN
    -- 执行第一个 INSERT 语句
    EXECUTE insert_sql1;

    -- 执行第二个 INSERT 语句
    EXECUTE insert_sql2;

    -- 执行并返回 SELECT 语句的结果
    RETURN QUERY EXECUTE select_sql;
END;
$$ LANGUAGE plpgsql;