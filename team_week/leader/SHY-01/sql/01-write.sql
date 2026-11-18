\pset pager off
fides_setup;
CREATE TABLE shy01_demo (ind text, keyword text SSE='FIDES', payload text);
INSERT INTO shy01_demo VALUES ('1','shy01_apple','red apple'),('2','shy01_apple','green apple'),('3','shy01_banana','yellow banana');
SELECT ind,keyword,payload FROM shy01_demo_data ORDER BY ind;
