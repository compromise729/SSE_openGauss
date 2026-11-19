-- ZYC-02/03 公开验收样例（SHY-02 验收样例与预期结果）
-- 前置：已在隔离实例执行 fides_setup; 与 sse_sql/SSE_v1.sql;
--       initialize-live <conn> <state>; 生成 v1 状态
-- 运行新版 SQL 需设置 SSE_V1_STATE_DIR=<state>

-- 1. 新密文表创建入口
CREATE TABLE week_demo (ind text, keyword text SSE='FIDES', payload text)
WITH (sse_storage='ROW_AEAD_V1');

-- 2. 三条公开数据写入（服务端只留密文与索引封装）
INSERT INTO week_demo VALUES
  ('1','apple','red apple'),
  ('2','apple','green apple'),
  ('3','banana','yellow banana');

-- 3. 物理表探针：应只有 Base64 密文（SSE1 封装），无明文
SELECT rid, data_blob FROM public.week_demo_data;
SELECT ut, locator_blob FROM public.week_demo_index;

-- 4. 检索（预期 apple 2 行 / banana 1 行 / unknown 0 行 / 重复 apple 仍 2 行）
SELECT * FROM week_demo WHERE keyword='apple';
SELECT * FROM week_demo WHERE keyword='banana';
SELECT * FROM week_demo WHERE keyword='zyc_unknown_20261119';
SELECT * FROM week_demo WHERE keyword='apple';

-- 5. 中文+空格关键词（预期 2 行）
INSERT INTO week_demo VALUES
  ('4','中文 苹果 space','red 苹果'),
  ('5','中文 苹果 space','green 苹果');
SELECT * FROM week_demo WHERE keyword='中文 苹果 space';

-- 6. SQL 双单引号转义（预期 1 行，保留 it's OR not / o'brien apple）
INSERT INTO week_demo VALUES ('6','it''s OR not','o''brien apple');
SELECT * FROM week_demo WHERE keyword='it''s OR not';

-- 7. 未支持语法（应在发送前拒绝，非零退出码，无任何明文输出）
SELECT * FROM week_demo WHERE keyword='apple' AND payload='red apple';
SELECT ind FROM week_demo WHERE keyword='apple';
