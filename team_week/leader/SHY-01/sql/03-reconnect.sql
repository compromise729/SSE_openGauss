\pset pager off
fides_setup;
SELECT * FROM shy01_demo WHERE keyword='shy01_apple';
SELECT * FROM shy01_demo WHERE keyword='shy01_unknown';
SELECT en_ind,length(ut),length(join_col) FROM shy01_demo_index ORDER BY en_ind;
