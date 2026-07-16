--fides
\i /opt/og/openGauss-server/sse_sql/SSE_init.sql   
CREATE TABLE fides (ind text, keyword text SSE='FIDES', values text);
INSERT INTO fides VALUES ('1', 'AALERUD Katrine', 'AALERUD K'),('2', 'ABAD Nestor', 'ABAD N');
select * from fides where keyword='AALERUD Katrine';
select * from fides where keyword='ABAD Nestor';

CREATE TABLE fides2 (ind text, keyword text SSE='FIDES', values1 text, values2 text);
INSERT INTO fides2 VALUES ('1', 'apple', 'big apple', 'small apple'),('2', 'banana', 'big banana', 'big banana');
INSERT INTO fides2 VALUES ('3', 'china apple', 'big apple', 'small apple');
select * from fides2 where keyword='apple';
select * from fides2 where keyword='china apple';


--mitra
\i /opt/og/openGauss-server/sse_sql/SSE_init.sql  
CREATE TABLE mitra (ind text, keyword text SSE='MITRA', values text);
INSERT INTO mitra VALUES ('1', 'AALERUD Katrine', 'AALERUD K'),('2', 'ABAD Nestor', 'ABAD N');
select * from mitra where keyword='AALERUD Katrine';
select * from mitra where keyword='ABAD Nestor';

CREATE TABLE mitra2 (ind text, keyword text SSE='MITRA', values1 text, values2 text);
INSERT INTO mitra2 VALUES ('1', 'apple', 'big apple', 'small apple'),('2', 'banana', 'big banana', 'big banana');
INSERT INTO mitra2 VALUES ('3', 'china apple', 'big apple', 'small apple');
select * from mitra2 where keyword='apple';
select * from mitra2 where keyword='china apple';


--FBDSSE
\i /opt/og/openGauss-server/sse_sql/SSE_init.sql  
CREATE TABLE FB (ind text, keyword text SSE='FBDSSE', values text);
insert into FB VALUES ('1', 'a', 'j1'),('2', 'b', 'j1'),('3', 'c', 'j2');
select * from fb where keyword='a';
insert into FB VALUES ('4', 'b', 'j5');
select * from fb where keyword='c';
select * from fb where keyword='b';
delete from FB where ind='2' and keyword='b';
select * from fb where keyword='a';

CREATE TABLE FB2 (ind text, keyword text SSE='FBDSSE', values1 text, values2 text);
insert into FB2 VALUES ('1', 'a', 'j1', 'small apple'),('2', 'b', 'j1', 'big banana'),('3', 'c', 'j2', 'big c');
select * from FB2 where keyword='a';
insert into FB2 VALUES ('4', 'a', 'j5', 'big apple');
select * from FB2 where keyword='a';
select * from FB2 where keyword='b';
delete from FB2 where ind='1' and keyword='a';
select * from FB2 where keyword='a';


--Orion
\i /opt/og/openGauss-server/sse_sql/SSE_init.sql  
CREATE TABLE orionT1 (ind text, keyword text SSE='ORION', values text);
orion_setup;
insert into orionT1 VALUES ('1', 'a', 'j1'),('2', 'b', 'j1'),('3', 'c', 'j2');
select * from orionT1 where keyword='b';
insert into orionT1 VALUES ('4', 'b', 'j5');
select * from orionT1 where keyword='a';
select * from orionT1 where keyword='b';
delete from orionT1 where ind='2' and keyword='b';
select * from orionT1 where keyword='b';

CREATE TABLE FB2 (ind text, keyword text SSE='FBDSSE', values1 text, values2 text);
insert into FB2 VALUES ('1', 'a', 'j1', 'small apple'),('2', 'b', 'j1', 'big banana'),('3', 'c', 'j2', 'big c');
select * from FB2 where keyword='a';
insert into FB2 VALUES ('4', 'a', 'j5', 'big apple');
select * from FB2 where keyword='a';
select * from FB2 where keyword='b';
delete from FB2 where ind='1' and keyword='a';
select * from FB2 where keyword='a';


-- closefb
-- chmod +x ./sse_sql/closefb_setup.sh
-- ./sse_sql/closefb_setup.sh t3
\i /opt/og/openGauss-server/sse_sql/SSE_init.sql
CREATE TABLE t3 (ind text, keyword text SSE='CLOSEFB', value text);
INSERT INTO t3 VALUES ('1', 'AALERUD Katrine', 'AALERUD K'),('2', 'ABAD Nestor', 'ABAD N');
select * from t3 where keyword='AALERUD Katrine';
select * from t3 where keyword='ABAD Nestor';

CREATE TABLE t4 (ind text, keyword text SSE='CLOSEFB', values1 text, values2 text);
INSERT INTO t4 VALUES ('1', 'apple', 'big apple', 'small apple'),('2', 'banana', 'big banana', 'big banana');
INSERT INTO t4 VALUES ('3', 'china apple', 'big apple', 'small apple');
select * from t4 where keyword='apple';
select * from t4 where keyword='china apple';


-- DUAL
-- chmod +x ./sse_sql/dual_setup.sh
-- ./sse_sql/dual_setup.sh t1
\i /opt/og/openGauss-server/sse_sql/SSE_init.sql
CREATE TABLE t1 (ind text, keyword text SSE='DUAL', value text);
INSERT INTO t1 VALUES ('1', 'AALERUD Katrine', 'AALERUD K'),('2', 'ABAD Nestor', 'ABAD N');
select * from t1 where keyword='AALERUD Katrine';
select * from t1 where keyword='ABAD Nestor';

CREATE TABLE t2 (ind text, keyword text SSE='DUAL', VALUES1 text, VALUES2 text);
INSERT INTO t2 VALUES ('1', 'apple', 'big apple', 'small apple'),('2', 'banana', 'big banana', 'big banana');
INSERT INTO t2 VALUES ('3', 'china apple', 'big apple', 'small apple');
select * from t2 where keyword='apple';
select * from t2 where keyword='china apple';



-- JANUSPP
\i /opt/og/openGauss-server/sse_sql/SSE_init.sql
CREATE TABLE t5 (ind text, keyword text SSE='JANUSPP', values text);
INSERT INTO t5 VALUES ('1', 'AALERUD Katrine', 'AALERUD K'),('2', 'ABAD Nestor', 'ABAD N');
select * from t5 where keyword='AALERUD Katrine';
select * from t5 where keyword='ABAD Nestor';

CREATE TABLE t6 (ind text, keyword text SSE='JANUSPP', values1 text, values2 text);
INSERT INTO t6 VALUES ('1', 'apple', 'big apple', 'small apple'),('2', 'banana', 'big banana', 'big banana');
INSERT INTO t6 VALUES ('3', 'china apple', 'big apple', 'small apple');
select * from t6 where keyword='apple';
select * from t6 where keyword='china apple';