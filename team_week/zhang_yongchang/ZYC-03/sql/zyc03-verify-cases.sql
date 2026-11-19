-- 先运行 ZYC-02 成功夹具；这里仅含成功交互，适用于 ON_ERROR_STOP=1。
-- 预期 apple 2 行、banana 1 行、unknown 0 行；重复 apple 不丢不重。
SELECT * FROM week_demo WHERE keyword='apple';
SELECT * FROM week_demo WHERE keyword='banana';
SELECT * FROM week_demo WHERE keyword='zyc_unknown_20261119';
SELECT * FROM week_demo WHERE keyword='apple';
INSERT INTO week_demo VALUES
  ('4','中文 苹果 space','red 苹果'),
  ('5','中文 苹果 space','green 苹果');
SELECT * FROM week_demo WHERE keyword='中文 苹果 space';
INSERT INTO week_demo VALUES ('6','it''s OR not','o''brien apple');
SELECT * FROM week_demo WHERE keyword='it''s OR not';
-- AND、投影、NULL 和超限输入的预期拒绝由自动入口单独执行及断言。
