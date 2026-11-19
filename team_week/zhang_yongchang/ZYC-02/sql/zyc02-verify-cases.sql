-- 手工成功夹具：仅在新隔离实例显式初始化 Fides、SSE_v1.sql 和 v1 状态后执行。
-- 完整验收使用 tools/zyc_live_verify_all.sh；故障 SQL 由工具逐次断言退出码。
CREATE TABLE week_demo (ind text, keyword text SSE='FIDES', payload text)
WITH (sse_storage='ROW_AEAD_V1');
INSERT INTO week_demo VALUES
  ('1','apple','red apple'),
  ('2','apple','green apple'),
  ('3','banana','yellow banana');
SELECT rid, data_blob FROM public.week_demo_data;
SELECT ut, locator_blob FROM public.week_demo_index;
