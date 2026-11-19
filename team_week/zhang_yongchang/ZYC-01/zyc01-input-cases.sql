-- ZYC-01 测试用例夹具（契约 v1.0 / SHY-02 release b6159cf8）
-- 目的：为 ZYC-02/03 连锁网准备输入分类与边界用例；本子任务不执行真实数据库。
-- 表名/关键词只用公开样例；真实建表/写入属于 ZYC-02。
--
-- 用法（ZYC-02/03 接口交付后，在隔离实例中）：
--   gsql -d postgres -p <port> -f 01-call-route-cases.sql
-- 现有防护只影响「已登记 Fides 逻辑表的单表 SELECT」，其它路径必须原样放行。

\echo '=== A. 新密文表 / 旧 SSE 表：受保护的单表查询（应进入检查并被支持）==='
SELECT * FROM week_demo WHERE keyword='apple';
SELECT * FROM week_demo WHERE keyword='banana';

\echo '=== B. 未支持语法（应在发送任何请求前明确拒绝，计数不变）==='
SELECT * FROM week_demo WHERE keyword='apple' AND ind='1';
SELECT payload FROM week_demo WHERE keyword='apple';
SELECT * FROM week_demo WHERE keyword=123;
SELECT * FROM week_demo WHERE keyword='apple' OR ind='2';
SELECT * FROM week_demo WHERE keyword LIKE 'app%';

\echo '=== C. 引号内 AND / OR 不是操作符（按完整字符串处理）==='
SELECT * FROM week_demo WHERE keyword='red AND green';
SELECT * FROM week_demo WHERE keyword='it''s OR not';

\echo '=== D. 中文 / 空格（按完整字符串保留）==='
SELECT * FROM week_demo WHERE keyword='中文 关键词';
SELECT * FROM week_demo WHERE keyword='红 苹果';

\echo '=== E. 普通表 / 物理表回查（必须原样放行，不误拦截）==='
SELECT * FROM public.orders WHERE id=1;
SELECT * FROM week_demo_data;
SELECT * FROM week_demo_index;
