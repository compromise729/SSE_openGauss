DROP FUNCTION IF EXISTS sse_create_tables2(TEXT, TEXT, TEXT, TEXT);
CREATE OR REPLACE FUNCTION sse_create_tables2(table_name TEXT, ind TEXT, keyword TEXT, value_col TEXT)
RETURNS VOID AS $$
DECLARE
    data_table_name TEXT := table_name || '_data';  -- 拼接data表名
    index_table_name TEXT := table_name || '_index';  -- 拼接index表名
    ut TEXT := 'ut';  -- 默认值
    en_ind TEXT := 'en_ind';
    join_col TEXT := 'join_col';
    value_cols TEXT[];  -- 用来存储 value_col 列名列表
    col_definitions TEXT := '';  -- 动态生成列定义的字符串
    i INTEGER;
BEGIN
    -- 将 value_col 列名按逗号拆分成数组
    value_cols := string_to_array(value_col, ',');

    -- 动态生成数据表的列定义（除了 ind 和 keyword）
    FOR i IN 1..array_length(value_cols, 1) LOOP
        -- 使用提供的列名创建对应的列定义
        col_definitions := col_definitions || format(', %I TEXT', trim(both ' ' from value_cols[i]));
    END LOOP;

    -- 创建表data
    -- EXECUTE format('
    --     CREATE TABLE IF NOT EXISTS %I (
    --         %I TEXT,
    --         %I TEXT
    --         %s
    --     )', lower(data_table_name), lower(ind), lower(keyword), col_definitions);
    EXECUTE format('
        CREATE TABLE IF NOT EXISTS %I (
            ind TEXT,
            %I TEXT
            %s
        )', lower(data_table_name), lower(keyword), col_definitions);

    -- 创建表index
    EXECUTE format('
        CREATE TABLE IF NOT EXISTS %I (
            %I TEXT,
            %I TEXT,
            %I TEXT
        )', lower(index_table_name), lower(ut), lower(en_ind), lower(join_col));

END;
$$ LANGUAGE plpgsql;


DROP FUNCTION IF EXISTS fbdsse_create_tables2(TEXT, TEXT, TEXT, TEXT);
CREATE OR REPLACE FUNCTION fbdsse_create_tables2(table_name TEXT, ind TEXT, keyword TEXT, value_col TEXT)
RETURNS VOID AS $$
DECLARE
    data_table_name TEXT := table_name || '_data';  -- 拼接data表名
    index_table_name TEXT := table_name || '_index';  -- 拼接index表名
    ut TEXT := 'ut';  -- 默认值
    en_ind TEXT := 'en_ind';
    FB_ct TEXT := 'FB_ct';
    value_cols TEXT[];  -- 用来存储 value_col 列名列表
    col_definitions TEXT := '';  -- 动态生成列定义的字符串
    i INTEGER;
BEGIN
    -- 将 value_col 列名按逗号拆分成数组
    value_cols := string_to_array(value_col, ',');

    -- 动态生成数据表的列定义（除了 ind 和 keyword）
    FOR i IN 1..array_length(value_cols, 1) LOOP
        -- 使用提供的列名创建对应的列定义
        col_definitions := col_definitions || format(', %I TEXT', trim(both ' ' from value_cols[i]));
    END LOOP;

    -- 创建表data
    -- EXECUTE format('
    --     CREATE TABLE IF NOT EXISTS %I (
    --         %I TEXT,
    --         %I TEXT
    --         %s
    --     )', lower(data_table_name), lower(ind), lower(keyword), col_definitions);
        EXECUTE format('
        CREATE TABLE IF NOT EXISTS %I (
            ind TEXT,
            %I TEXT
            %s
        )', lower(data_table_name), lower(keyword), col_definitions);

    -- 创建表index
    EXECUTE format('
        CREATE TABLE IF NOT EXISTS %I (
            %I TEXT,
            %I TEXT,
            %I TEXT
        )', lower(index_table_name), lower(ut), lower(en_ind), lower(FB_ct));

END;
$$ LANGUAGE plpgsql;


---------INSERT---------
DROP FUNCTION IF EXISTS fides_insert(TEXT[], TEXT[], TEXT[], TEXT, TEXT);
-- CREATE OR REPLACE FUNCTION fides_insert(p_ind text[], p_ut text[], p_join text[], table_prefix TEXT, data_sql TEXT) RETURNS VOID AS $$
-- DECLARE
--     index_table_name TEXT := table_prefix || '_index';  -- 拼接index表名
--     index_sql TEXT;
-- BEGIN
--     -- 构建插入语句
--     index_sql := format('INSERT INTO %I (UT, en_ind, join_col) VALUES ', index_table_name);
--     FOR i IN 1..array_length(p_ind, 1) LOOP
--         index_sql := index_sql || '(' || quote_literal(p_ut[i]) || ', ' || quote_literal(p_ind[i] || '|add') || ', ' || quote_literal(p_join[i]) || '),';
--     END LOOP;
--     -- 去除最后一个逗号
--     index_sql := left(index_sql, length(index_sql) - 1);
--     -- 执行插入语句
--     EXECUTE data_sql;
--     EXECUTE index_sql;
-- END;
-- $$ LANGUAGE plpgsql;
CREATE OR REPLACE FUNCTION fides_insert(
    p_ind text[], 
    p_ut text[], 
    p_join text[], 
    table_prefix TEXT, 
    data_sql TEXT
) RETURNS VOID AS $$
DECLARE
    index_table_name TEXT := table_prefix || '_index';
BEGIN
    -- 先执行数据插入语句
    EXECUTE data_sql;

    -- 用 generate_subscripts 高效插入 index 表
    EXECUTE format($fmt$
        INSERT INTO %I (UT, en_ind, join_col)
        SELECT $1[i], $2[i] || '|add', $3[i]
        FROM generate_subscripts($1, 1) AS i
    $fmt$, index_table_name)
    USING p_ut, p_ind, p_join;
END;
$$ LANGUAGE plpgsql;



DROP FUNCTION IF EXISTS mitra_insert(TEXT[], TEXT[], TEXT[], TEXT, TEXT);
CREATE OR REPLACE FUNCTION mitra_insert(p_addr text[], p_val text[], p_join text[], table_prefix TEXT, data_sql TEXT) RETURNS VOID AS $$
DECLARE
    index_table_name TEXT := table_prefix || '_index';  -- 拼接index表名
    index_sql TEXT;
BEGIN
    -- 构建插入语句
    index_sql := format('INSERT INTO %I (ut, en_ind, join_col) VALUES ', index_table_name);
    FOR i IN 1..array_length(p_addr, 1) LOOP
        index_sql := index_sql || '(' || quote_literal(p_addr[i]) || ', ' || quote_literal(p_val[i]) || ', ' || quote_literal(p_join[i]) || '),';
    END LOOP;
    -- 去除最后一个逗号
    index_sql := left(index_sql, length(index_sql) - 1);
    -- 执行插入语句
    EXECUTE data_sql;
    EXECUTE index_sql;
END;
$$ LANGUAGE plpgsql;


DROP FUNCTION IF EXISTS fbdsse_insert(TEXT[], TEXT[], TEXT[], TEXT, TEXT);
CREATE OR REPLACE FUNCTION fbdsse_insert(
  p_ut text[],
  p_e text[],
  p_ct text[],
  table_prefix TEXT,
  data_sql TEXT
) RETURNS VOID AS $$
DECLARE
    index_table_name TEXT := table_prefix || '_index';
BEGIN
    -- 使用事务封装，确保一致性并优化性能
    PERFORM pg_advisory_xact_lock(1); -- 可选防并发写入（如果你希望串行写）

    -- 执行外部插入语句（比如插入主数据表）
    EXECUTE data_sql;

    -- 用 loop 插入 index 表（避免字符串拼接）
    FOR i IN array_lower(p_ut, 1)..array_upper(p_ut, 1) LOOP
        EXECUTE format(
          'INSERT INTO %I (UT, en_ind, FB_ct) VALUES ($1, $2, $3)',
          index_table_name
        ) USING p_ut[i], p_e[i], p_ct[i];
    END LOOP;

END;
$$ LANGUAGE plpgsql;

-- postgresql版本太老，不支持这个用法
-- CREATE OR REPLACE FUNCTION fbdsse_insert(
--     p_ut text[],
--     p_e text[],
--     p_ct text[],
--     table_prefix TEXT,
--     data_sql TEXT
-- ) RETURNS VOID AS $$
-- DECLARE
--     index_table_name TEXT := table_prefix || '_index';
-- BEGIN
--     -- 执行 data_sql 语句（外部构造）
--     EXECUTE data_sql;

--     -- 使用 unnest 优化插入
--     EXECUTE format(
--         'INSERT INTO %I (UT, en_ind, FB_ct) SELECT * FROM unnest($1::text[], $2::text[], $3::text[])',
--         index_table_name
--     )
--     USING p_ut, p_e, p_ct;
-- END;
-- $$ LANGUAGE plpgsql;




----------SELECT----------
-- DROP FUNCTION IF EXISTS fides_select(VARCHAR[], VARCHAR[], VARCHAR[], TEXT);
-- CREATE OR REPLACE FUNCTION fides_select(
--     p_ut VARCHAR[], 
--     p_ind VARCHAR[], 
--     p_join VARCHAR[], 
--     table_prefix TEXT      -- 表名前缀
-- ) RETURNS TABLE(ind TEXT, keyword TEXT, context TEXT) AS $$
-- DECLARE
--     insert_sql TEXT;
--     search_sql TEXT;
--     data_table_name TEXT := table_prefix || '_data';  -- 拼接data表名
--     index_table_name TEXT := table_prefix || '_index';  -- 拼接index表名
-- BEGIN
--     -- 构建插入数据索引的动态 SQL 语句
--     insert_sql := format('INSERT INTO %I (UT, en_ind, join_col) VALUES ', index_table_name);
--     FOR i IN 1..array_length(p_ind, 1) LOOP
--         insert_sql := insert_sql || '(' || quote_literal(p_ut[i]) || ', ' || quote_literal(p_ind[i] || '|add') || ', ' || quote_literal(p_join[i] ) || '),';
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


-- DROP FUNCTION IF EXISTS mitra_select(VARCHAR[], VARCHAR[], VARCHAR[], VARCHAR[], TEXT);
-- CREATE OR REPLACE FUNCTION mitra_select(p_addr VARCHAR[], p_val VARCHAR[], p_join VARCHAR[], p_ind VARCHAR[], table_prefix TEXT)
-- RETURNS TABLE(ind TEXT, keyword TEXT, context TEXT) AS $$
-- DECLARE
--     insert_sql TEXT;
--     search_sql TEXT;
--     data_table_name TEXT := table_prefix || '_data';  -- 拼接data表名
--     index_table_name TEXT := table_prefix || '_index';  -- 拼接index表名
-- BEGIN
--     -- 构建插入数据的动态 SQL 语句
--     -- insert_sql := format('INSERT INTO %I (ut, en_ind, join_col) VALUES ', index_table_name);
--     insert_sql := format('INSERT INTO %I  VALUES ', index_table_name);
--     FOR i IN 1..array_length(p_addr, 1) LOOP
--         insert_sql := insert_sql || '(' || quote_literal(p_addr[i]) || ', ' || quote_literal(p_val[i])  || ', ' || quote_literal(p_join[i] ) || '),';
--     END LOOP;
--     insert_sql := rtrim(insert_sql, ',');
    
--     -- 执行插入操作
--     EXECUTE insert_sql;
    
--     -- 构建搜索数据的动态 SQL 语句
--     search_sql := format('SELECT * FROM %I WHERE ind IN (', data_table_name);
--     FOR i IN 1..array_length(p_ind, 1) LOOP
--         search_sql := search_sql || quote_literal(p_ind[i]) || ', ';
--     END LOOP;
--     search_sql := rtrim(search_sql, ', ');--逗号后面有个空格
--     search_sql := search_sql || ')';
--     -- 执行搜索操作并将结果返回
--     RETURN QUERY EXECUTE search_sql;
-- END;
-- $$ LANGUAGE plpgsql;


-- DROP FUNCTION IF EXISTS fbdsse_select(TEXT , TEXT);
-- CREATE OR REPLACE FUNCTION fbdsse_select(
--     insert_sql text,
--     select_sql text
-- ) RETURNS TABLE (ind TEXT, keyword TEXT, context TEXT) AS $$
-- BEGIN
--     -- 执行 INSERT 语句
--     EXECUTE insert_sql;

--     -- 执行并返回 SELECT 语句的结果
--     RETURN QUERY EXECUTE select_sql;
-- END;
-- $$ LANGUAGE plpgsql;


DROP FUNCTION IF EXISTS fides_re_insert(TEXT[], TEXT[], TEXT[], TEXT);
CREATE OR REPLACE FUNCTION fides_re_insert(
    p_ut TEXT[], 
    p_ind TEXT[], 
    p_join TEXT[], 
    table_prefix TEXT
)
RETURNS VOID  AS $$
DECLARE
    insert_sql TEXT;
    index_table_name TEXT := table_prefix || '_index';  -- 拼接 index 表名
BEGIN
    -- 构建插入数据的动态 SQL 语句
    insert_sql := format('INSERT INTO %I (UT, en_ind, join_col) VALUES ', index_table_name);
    FOR i IN 1..array_length(p_ind, 1) LOOP
        insert_sql := insert_sql || '(' || quote_literal(p_ut[i]) || ', ' || quote_literal(p_ind[i] || '|add') || ', ' || quote_literal(p_join[i]) || '),';
    END LOOP;
    insert_sql := rtrim(insert_sql, ',');

    -- 执行插入操作
    EXECUTE insert_sql;   
END;
$$ LANGUAGE plpgsql;

DROP FUNCTION IF EXISTS mitra_re_insert(TEXT[], TEXT[], TEXT[], TEXT);
CREATE OR REPLACE FUNCTION mitra_re_insert(p_addr TEXT[], p_val TEXT[], p_join TEXT[], table_prefix TEXT)
RETURNS VOID  AS $$
DECLARE
    insert_sql TEXT;
    index_table_name TEXT := table_prefix || '_index';  -- 拼接index表名
BEGIN
    -- 构建插入数据的动态 SQL 语句
    insert_sql := format('INSERT INTO %I  VALUES ', index_table_name);
    FOR i IN 1..array_length(p_addr, 1) LOOP
        insert_sql := insert_sql || '(' || quote_literal(p_addr[i]) || ', ' || quote_literal(p_val[i])  || ', ' || quote_literal(p_join[i] ) || '),';
    END LOOP;
    insert_sql := rtrim(insert_sql, ',');
    
    -- 执行插入操作
    EXECUTE insert_sql;
END;
$$ LANGUAGE plpgsql;


DROP FUNCTION IF EXISTS fbdsse_select(TEXT);
CREATE OR REPLACE FUNCTION fbdsse_select(
    insert_sql text
) RETURNS VOID AS $$
BEGIN
    -- 执行 INSERT 语句
    EXECUTE insert_sql;
END;
$$ LANGUAGE plpgsql;




----------JOIN----------
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


DROP FUNCTION IF EXISTS mitra_join(TEXT , TEXT, TEXT);
CREATE OR REPLACE FUNCTION mitra_join(
    exec_sql1 text,
    exec_sql2 text,
    select_sql text
) RETURNS TABLE (t1_ind TEXT, t1_keyword TEXT, t1_context TEXT, t2_ind TEXT, t2_keyword TEXT, t2_context TEXT) AS $$
DECLARE
    -- search_sql TEXT;
BEGIN
    -- 执行第一个 语句
    EXECUTE exec_sql1;

    -- 执行第二个语句
    EXECUTE exec_sql2;


    -- search_sql := format('%L', select_sql);
    -- search_sql := rtrim(search_sql, ';');
    -- 执行搜索操作并将结果返回
    RETURN QUERY EXECUTE select_sql;

END;
$$ LANGUAGE plpgsql;





--------component-------
DROP FUNCTION IF EXISTS mitra_reinsert(VARCHAR[], VARCHAR[], VARCHAR[], TEXT);
CREATE OR REPLACE FUNCTION mitra_reinsert(p_addr VARCHAR[], p_val VARCHAR[], p_join VARCHAR[], table_prefix TEXT)
RETURNS VOID AS $$
DECLARE
    insert_sql TEXT;
    index_table_name TEXT := table_prefix || '_index';  -- 拼接index表名
BEGIN
    -- 构建插入数据的动态 SQL 语句
    -- insert_sql := format('INSERT INTO %I (ut, en_ind, join_col) VALUES ', index_table_name);
    insert_sql := format('INSERT INTO %I  VALUES ', index_table_name);
    FOR i IN 1..array_length(p_addr, 1) LOOP
        insert_sql := insert_sql || '(' || quote_literal(p_addr[i]) || ', ' || quote_literal(p_val[i])  || ', ' || quote_literal(p_join[i] ) || '),';
    END LOOP;
    insert_sql := rtrim(insert_sql, ',');
    
    -- 执行插入操作
    EXECUTE insert_sql;
    
END;
$$ LANGUAGE plpgsql;



-- closefb.sql
DROP FUNCTION IF EXISTS closefb_create_tables2(TEXT, TEXT, TEXT, TEXT);
CREATE OR REPLACE FUNCTION closefb_create_tables2(table_name TEXT, ind TEXT, keyword TEXT, value_col TEXT)
RETURNS VOID AS $$
DECLARE
    data_table_name TEXT := table_name || '_data';  
    index_table_name TEXT := table_name || '_index';  
    indexkey TEXT := 'indexkey';  
    indexvalue TEXT := 'indexvalue';
    value_cols TEXT[];  -- 用来存储 value_col 列名列表
    col_definitions TEXT := '';  -- 动态生成列定义的字符串
    i INTEGER;
BEGIN
    -- 将 value_col 列名按逗号拆分成数组
    value_cols := string_to_array(value_col, ',');

    -- 动态生成数据表的列定义（除了 ind 和 keyword）
    FOR i IN 1..array_length(value_cols, 1) LOOP
        -- 使用提供的列名创建对应的列定义
        col_definitions := col_definitions || format(', %I TEXT', trim(both ' ' from value_cols[i]));
    END LOOP;

    -- 创建表data
    -- EXECUTE format('
    --     CREATE TABLE IF NOT EXISTS %I (
    --         %I TEXT,
    --         %I TEXT
    --         %s
    --     )', lower(data_table_name), lower(ind), lower(keyword), col_definitions);
    EXECUTE format('
        CREATE TABLE IF NOT EXISTS %I (
            ind TEXT,
            keyword TEXT
            %s
        )', lower(data_table_name), col_definitions);

    -- 创建 index 表
    EXECUTE format('
        CREATE TABLE IF NOT EXISTS %I (
            %I TEXT,
            %I TEXT
        )', lower(index_table_name), lower(indexkey), lower(indexvalue));

END;
$$ LANGUAGE plpgsql;


DROP FUNCTION IF EXISTS closefb_insert(TEXT[], TEXT[], TEXT, TEXT);
CREATE OR REPLACE FUNCTION closefb_insert(p_indexkey text[], p_indexvalue text[], table_prefix TEXT, data_sql TEXT) RETURNS VOID AS $$
DECLARE
    index_table_name TEXT := table_prefix || '_index';  -- 拼接index表名
    index_sql TEXT;
BEGIN
    -- 构建插入语句    
    index_sql := format('INSERT INTO %I (indexkey, indexvalue) VALUES ', index_table_name);
    FOR i IN 1..array_length(p_indexkey, 1) LOOP
        index_sql := index_sql || '(' || quote_literal(p_indexkey[i]) || ', ' || quote_literal(p_indexvalue[i]) || '),';
    END LOOP;
    -- 去除最后一个逗号
    index_sql := left(index_sql, length(index_sql) - 1);
    -- 执行插入语句
    EXECUTE data_sql;
    EXECUTE index_sql;
END;
$$ LANGUAGE plpgsql;



DROP FUNCTION IF EXISTS closefb_delete(TEXT[], TEXT[], TEXT[], TEXT[], TEXT);
CREATE OR REPLACE FUNCTION closefb_delete(p_ind text[], p_keyword text[], p_indexkey text[], p_indexvalue text[], table_prefix TEXT) RETURNS VOID AS $$
DECLARE
    sql_query1 TEXT;
    sql_query2 TEXT;
    head_sql TEXT;
    index_table_name TEXT := table_prefix || '_index';  -- 拼接index表名
    data_table_name TEXT := table_prefix || '_data'; -- 拼接data表名
BEGIN
    -- 构建删除语句    
    head_sql := format('DELETE FROM %I WHERE ', data_table_name);
    sql_query2 := format('INSERT INTO %I (indexkey, indexvalue) VALUES ', index_table_name);
    FOR i IN 1..array_length(p_ind, 1) LOOP
        sql_query1 := head_sql || 'ind = ' || quote_literal(p_ind[i]) || 'and keyword = ' || quote_literal(p_keyword[i]);
        EXECUTE sql_query1;
    END LOOP;

    FOR i IN 1..array_length(p_indexkey, 1) LOOP
        sql_query2 := sql_query2 || '(' || quote_literal(p_indexkey[i]) || ', ' || quote_literal(p_indexvalue[i]) || '),';
    END LOOP;
    -- 去除最后一个逗号
    sql_query2 := left(sql_query2, length(sql_query2) - 1);
    -- 执行插入语句
    EXECUTE sql_query2;
END;
$$ LANGUAGE plpgsql;

DROP FUNCTION IF EXISTS closefb_select(TEXT[], table_prefix TEXT);
CREATE OR REPLACE FUNCTION closefb_select(p_ind text[], table_prefix TEXT)
RETURNS SETOF RECORD AS $$
DECLARE
    search_sql TEXT;
    data_table_name TEXT := table_prefix || '_data'; -- 拼接data表名
BEGIN
    search_sql := format('SELECT * FROM %I WHERE ind IN (', data_table_name);
    FOR i IN 1..array_length(p_ind, 1) LOOP
        search_sql := search_sql || quote_literal(p_ind[i]) || ', ';
    END LOOP;
    search_sql := rtrim(search_sql, ', ');--逗号后面有个空格
    search_sql := search_sql || ')';

    -- 执行搜索操作并将结果返回
    RETURN QUERY EXECUTE search_sql;
END;
$$ LANGUAGE plpgsql;



-- DUAL.sql
DROP FUNCTION IF EXISTS dual_create_tables2(TEXT, TEXT, TEXT, TEXT);
CREATE OR REPLACE FUNCTION dual_create_tables2(table_name TEXT, ind TEXT, keyword TEXT, value_col TEXT)
RETURNS VOID AS $$
DECLARE
    data_table_name TEXT := table_name || '_data';  
    index_table_name TEXT := table_name || '_index';  
    label1 TEXT := 'label1';  
    label2 TEXT := 'label2';
    encdata TEXT := 'data';
    value_cols TEXT[];  -- 用来存储 value_col 列名列表
    col_definitions TEXT := '';  -- 动态生成列定义的字符串
    i INTEGER;
BEGIN
    -- 将 value_col 列名按逗号拆分成数组
    value_cols := string_to_array(value_col, ',');

    -- 动态生成数据表的列定义（除了 ind 和 keyword）
    FOR i IN 1..array_length(value_cols, 1) LOOP
        -- 使用提供的列名创建对应的列定义
        col_definitions := col_definitions || format(', %I TEXT', trim(both ' ' from value_cols[i]));
    END LOOP;

    -- 创建表data
    -- EXECUTE format('
    --     CREATE TABLE IF NOT EXISTS %I (
    --         %I TEXT,
    --         %I TEXT
    --         %s
    --     )', lower(data_table_name), lower(ind), lower(keyword), col_definitions);
    EXECUTE format('
        CREATE TABLE IF NOT EXISTS %I (
            ind TEXT,
            %I TEXT
            %s
        )', lower(data_table_name), lower(keyword), col_definitions);

    -- 创建 index 表
    EXECUTE format('
        CREATE TABLE IF NOT EXISTS %I (
            %I TEXT,
            %I TEXT,
            %I TEXT
        )', lower(index_table_name), lower(label1), lower(label2), lower(encdata));

END;
$$ LANGUAGE plpgsql;


DROP FUNCTION IF EXISTS dual_insert(TEXT[], TEXT[], TEXT[], TEXT, TEXT);
-- CREATE OR REPLACE FUNCTION dual_insert(p_label1 text[], p_label2 text[], p_data text[], table_prefix TEXT, data_sql TEXT) RETURNS VOID AS $$
-- DECLARE
--     index_table_name TEXT := table_prefix || '_index';  -- 拼接index表名
--     index_sql TEXT;
-- BEGIN
--     -- 构建插入语句
--     index_sql := format('INSERT INTO %I (label1, label2, data) VALUES ', index_table_name);
--     FOR i IN 1..array_length(p_label1, 1) LOOP
--         index_sql := index_sql || '(' || quote_literal(p_label1[i]) || ', ' || quote_literal(p_label2[i]) || ', ' || quote_literal(p_data[i]) || '),';
--     END LOOP;
--     -- 去除最后一个逗号
--     index_sql := left(index_sql, length(index_sql) - 1);
--     -- 执行插入语句
--     EXECUTE data_sql;
--     EXECUTE index_sql;
-- END;
-- $$ LANGUAGE plpgsql;
CREATE OR REPLACE FUNCTION dual_insert(
    p_label1 text[], 
    p_label2 text[], 
    p_data text[], 
    table_prefix TEXT, 
    data_sql TEXT
) RETURNS VOID AS $$
DECLARE
    index_table_name TEXT := table_prefix || '_index';
BEGIN
    -- 执行 data_sql 插入数据
    EXECUTE data_sql;

    -- 正确使用 $1, $2, $3 并通过 USING 传递参数
    EXECUTE format($fmt$
        INSERT INTO %I (label1, label2, data)
        SELECT $1[i], $2[i], $3[i]
        FROM generate_subscripts($1, 1) AS i
    $fmt$, index_table_name)
    USING p_label1, p_label2, p_data;
END;
$$ LANGUAGE plpgsql;


-- 执行删除操作：索引表和数据表两表删除
DROP FUNCTION IF EXISTS dual_delete(TEXT[], TEXT[], TEXT);
CREATE OR REPLACE FUNCTION dual_delete(p_ind text[], p_label1 text[], table_prefix TEXT) RETURNS VOID AS $$
DECLARE
    sql_query1 TEXT;
    sql_query2 TEXT;
    index_table_name TEXT := table_prefix || '_index';  -- 拼接index表名
    data_table_name TEXT := table_prefix || '_data'; -- 拼接data表名
BEGIN
    -- 构建删除语句    
    sql_query1 := format('DELETE FROM %I WHERE ind IN (', data_table_name);
    sql_query2 := format('DELETE FROM %I WHERE label1 IN (', index_table_name);
    FOR i IN 1..array_length(p_ind, 1) LOOP
        sql_query1 := sql_query1 || quote_literal(p_ind[i]) || ', ';
    END LOOP;
    FOR i IN 1..array_length(p_label1, 1) LOOP
        sql_query2 := sql_query2 || quote_literal(p_label1[i]) || ', ';
    END LOOP;
    sql_query1 := rtrim(sql_query1, ', ');--逗号后面有个空格
    sql_query1 := sql_query1 || ')';
    sql_query2 := rtrim(sql_query2, ', ');--逗号后面有个空格
    sql_query2 := sql_query2 || ')';
    -- 执行删除语句
    EXECUTE sql_query1;
    EXECUTE sql_query2;
END;
$$ LANGUAGE plpgsql;


-- 获取ind后执行搜索：重新更新索引并搜索数据
DROP FUNCTION IF EXISTS dual_select(TEXT[], TEXT);
CREATE OR REPLACE FUNCTION dual_select(p_ind text[], table_prefix TEXT)
RETURNS SETOF RECORD AS $$
DECLARE
    search_sql TEXT;
    data_table_name TEXT := table_prefix || '_data'; -- 拼接data表名
BEGIN
    -- 构建搜索数据的动态 SQL 语句
    search_sql := format('SELECT * FROM %I WHERE ind IN (', data_table_name);
    FOR i IN 1..array_length(p_ind, 1) LOOP
        search_sql := search_sql || quote_literal(p_ind[i]) || ', ';
    END LOOP;
    search_sql := rtrim(search_sql, ', ');--逗号后面有个空格
    search_sql := search_sql || ')';

    -- 执行搜索操作并将结果返回
    RETURN QUERY EXECUTE search_sql;
END;
$$ LANGUAGE plpgsql;


-- januspp
DROP FUNCTION IF EXISTS januspp_create_tables2(TEXT, TEXT, TEXT, TEXT);
CREATE OR REPLACE FUNCTION januspp_create_tables2(table_name TEXT, ind TEXT, keyword TEXT, value_col TEXT)
RETURNS VOID AS $$
DECLARE
    data_table_name TEXT := table_name || '_data';  
    indexadd_table_name TEXT := table_name || '_indexadd';  
    indexdel_table_name TEXT := table_name || '_indexdel';
    cache_table_name TEXT := table_name || '_cache';
    enckeyword TEXT := 'enckeyword';  
    encind TEXT := 'encind';
    tag TEXT := 'tag';
    punckey TEXT := 'punckey';
    deltag TEXT := 'deltag';
    tkn TEXT := 'tkn';
    value_cols TEXT[];  -- 用来存储 value_col 列名列表
    col_definitions TEXT := '';  -- 动态生成列定义的字符串
    i INTEGER;
BEGIN
    -- 将 value_col 列名按逗号拆分成数组
    value_cols := string_to_array(value_col, ',');

    -- 动态生成数据表的列定义（除了 ind 和 keyword）
    FOR i IN 1..array_length(value_cols, 1) LOOP
        -- 使用提供的列名创建对应的列定义
        col_definitions := col_definitions || format(', %I TEXT', trim(both ' ' from value_cols[i]));
    END LOOP;

    -- 创建表data
    -- EXECUTE format('
    --     CREATE TABLE IF NOT EXISTS %I (
    --         %I TEXT,
    --         %I TEXT
    --         %s
    --     )', lower(data_table_name), lower(ind), lower(keyword), col_definitions);
    EXECUTE format('
        CREATE TABLE IF NOT EXISTS %I (
            ind TEXT,
            keyword TEXT
            %s
        )', lower(data_table_name), col_definitions);

    -- 创建 indexadd 表
    EXECUTE format('
        CREATE TABLE IF NOT EXISTS %I (
            %I TEXT,
            %I TEXT,
            %I TEXT
        )', lower(indexadd_table_name), lower(enckeyword), lower(encind), lower(tag));

    -- 创建 indexdel 表
    EXECUTE format('
        CREATE TABLE IF NOT EXISTS %I (
            %I TEXT,
            %I TEXT,
            %I TEXT
        )', lower(indexdel_table_name), lower(enckeyword), lower(punckey), lower(deltag));

    -- 创建 cache 表
    -- EXECUTE format('
    --     CREATE TABLE IF NOT EXISTS %I (
    --         %I TEXT,
    --         %I TEXT,
    --         %I TEXT
    --     )', lower(cache_table_name), lower(tkn), lower(ind), lower(tag));
    EXECUTE format('
        CREATE TABLE IF NOT EXISTS %I (
            %I TEXT,
            ind TEXT,
            %I TEXT
        )', lower(cache_table_name), lower(tkn), lower(tag));

END;
$$ LANGUAGE plpgsql;

DROP FUNCTION IF EXISTS januspp_insert(TEXT[], TEXT[], TEXT[], TEXT, TEXT);
CREATE OR REPLACE FUNCTION januspp_insert(p_enckeyword text[], p_encind text[], p_tag text[], table_prefix TEXT, data_sql TEXT) RETURNS VOID AS $$
DECLARE
    indexadd_table_name TEXT := table_prefix || '_indexadd';
    indexadd_sql TEXT;
BEGIN
    -- 构建插入语句    
    indexadd_sql := format('INSERT INTO %I (enckeyword, encind, tag) VALUES ', indexadd_table_name);
    FOR i IN 1..array_length(p_enckeyword, 1) LOOP
        indexadd_sql := indexadd_sql || '(' || quote_literal(p_enckeyword[i]) || ', ' || quote_literal(p_encind[i]) || ', ' || quote_literal(p_tag[i]) || '),';
    END LOOP;
    -- 去除最后一个逗号
    indexadd_sql := left(indexadd_sql, length(indexadd_sql) - 1);
    -- 执行插入语句
    EXECUTE data_sql;
    EXECUTE indexadd_sql;
END;
$$ LANGUAGE plpgsql;


DROP FUNCTION IF EXISTS januspp_delete(TEXT[], TEXT[], TEXT[], TEXT[], TEXT[], TEXT);
CREATE OR REPLACE FUNCTION januspp_delete(p_ind text[], p_keyword text[], p_enckeyword text[], p_punckey text[], p_deltag text[], table_prefix TEXT) RETURNS VOID AS $$
DECLARE
    sql_query1 TEXT;
    sql_query2 TEXT;
    indexdel_table_name TEXT := table_prefix || '_indexdel';
    data_table_name TEXT := table_prefix || '_data'; 
BEGIN
    -- 构建删除语句    
    sql_query1 := format('DELETE FROM %I WHERE ', data_table_name);
    sql_query2 := format('INSERT INTO %I (enckeyword, punckey, deltag) VALUES ', indexdel_table_name);
    FOR i IN 1..array_length(p_ind, 1) LOOP
        sql_query1 := sql_query1 || 'ind = ' || quote_literal(p_ind[i]) || 'and keyword = ' || quote_literal(p_keyword[i]);
    END LOOP;

    FOR i IN 1..array_length(p_punckey, 1) LOOP
        sql_query2 := sql_query2 || '(' || quote_literal(p_enckeyword[i]) || ', ' || quote_literal(p_punckey[i]) || ', ' || quote_literal(p_deltag[i]) || '),';
    END LOOP;
    -- 去除最后一个逗号
    sql_query2 := left(sql_query2, length(sql_query2) - 1);
    -- 执行插入语句
    EXECUTE sql_query1;
    EXECUTE sql_query2;
END;
$$ LANGUAGE plpgsql;

DROP FUNCTION IF EXISTS januspp_select(TEXT[], TEXT);
CREATE OR REPLACE FUNCTION januspp_select(p_ind text[], table_prefix TEXT)
RETURNS SETOF RECORD AS $$
DECLARE
    sql_query1 TEXT;
    data_table_name TEXT := table_prefix || '_data'; 
BEGIN
    sql_query1 := format('SELECT * FROM %I WHERE ind IN (', data_table_name);
    FOR i IN 1..array_length(p_ind, 1) LOOP
        sql_query1 := sql_query1 || quote_literal(p_ind[i]) || ', ';
    END LOOP;
    sql_query1 := rtrim(sql_query1, ', ');--逗号后面有个空格
    sql_query1 := sql_query1 || ')';
    -- 执行搜索操作并将结果返回
    RETURN QUERY EXECUTE sql_query1;
END;
$$ LANGUAGE plpgsql;



-- ORION
DROP FUNCTION IF EXISTS orion_create_tables2(TEXT, TEXT, TEXT, TEXT);
CREATE OR REPLACE FUNCTION orion_create_tables2(table_name TEXT, ind TEXT, keyword TEXT, value_col TEXT)
RETURNS VOID AS $$
DECLARE
    data_table_name TEXT := table_name || '_data';  -- 拼接data表名
    value_cols TEXT[];  -- 用来存储 value_col 列名列表
    col_definitions TEXT := '';  -- 动态生成列定义的字符串
    i INTEGER;
BEGIN
    -- 将 value_col 列名按逗号拆分成数组
    value_cols := string_to_array(value_col, ',');

    -- 动态生成数据表的列定义（除了 ind 和 keyword）
    FOR i IN 1..array_length(value_cols, 1) LOOP
        -- 使用提供的列名创建对应的列定义
        col_definitions := col_definitions || format(', %I TEXT', trim(both ' ' from value_cols[i]));
    END LOOP;

    EXECUTE format('
        CREATE TABLE IF NOT EXISTS %I (
            ind TEXT,
            %I TEXT
            %s
        )', lower(data_table_name), lower(keyword), col_definitions);


END;
$$ LANGUAGE plpgsql;