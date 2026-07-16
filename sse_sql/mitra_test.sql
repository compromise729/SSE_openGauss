\i /opt/og/openGauss-server/sse_sql/SSE_init.sql
CREATE TABLE mitra (ind text, keyword text SSE='MITRA', values text);


INSERT INTO mitra VALUES ('1', 'AALERUD Katrine', 'AALERUD K'),('2', 'ABAD Nestor', 'ABAD N');
select * from mitra where keyword='AALERUD Katrine';
select * from mitra where keyword='ABAD Nestor';

INSERT INTO mitra VALUES ('1', 'a', 'a111'),('2', 'a', 'a222'),('3', 'a', 'a333');
delete from mitra where ind='3' and keyword='a';
select * from mitra where keyword='a';


CREATE TABLE mitraT1 (ind text, keyword text SSE='Mitra', values text);
CREATE TABLE mitraT2 (ind text, keyword text SSE='Mitra', values text);

insert into mitraT1 VALUES ('1', 'a', 'j1'),('2', 'b', 'j2'),('3', 'c', 'j3'),('4', 'd', 'j4');
insert into mitraT2 VALUES ('1', 'e', 'j1'),('2', 'f', 'j2'),('3', 'g', 'j3'),('4', 'h', 'j4');

insert into mitraT1 VALUES ('5', 'e', 'j1'),('6', 'f', 'j1'),('7', 'g', 'j2'),('8', 'h', 'j3');
insert into mitraT2 VALUES ('5', 'a', 'j1'),('6', 'b', 'j2'),('7', 'c', 'j4'),('8', 'd', 'j4');

SELECT * FROM mitraT1 join mitraT2 on mitraT1.values=mitraT2.values WHERE mitraT1.keyword='a' and mitraT2.keyword='e'; 
SELECT * FROM mitraT1 join mitraT2 on mitraT1.values=mitraT2.values WHERE mitraT1.keyword='b' and mitraT2.keyword='f'; 
SELECT * FROM mitraT1 LEFT join mitraT2 on mitraT1.values=mitraT2.values WHERE mitraT1.keyword='a' and mitraT2.keyword='d'; 
SELECT * FROM mitraT1 RIGHT join mitraT2 on mitraT1.values=mitraT2.values WHERE mitraT1.keyword='a' and mitraT2.keyword='f'; 
SELECT * FROM mitraT1 CROSS join mitraT2 WHERE mitraT1.keyword='b' and mitraT2.keyword='h';