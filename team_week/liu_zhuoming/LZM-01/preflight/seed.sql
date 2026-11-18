-- Use only after "isolated.sh init", before any SSE table exists.
\i :SSE_INIT_SQL
fides_setup;
CREATE TABLE team_fides_demo (ind text, keyword text SSE='FIDES', payload text);
INSERT INTO team_fides_demo VALUES
  ('1', 'teamdemo20261012apple', 'red apple'),
  ('2', 'teamdemo20261012apple', 'green apple'),
  ('3', 'teamdemo20261012banana', 'yellow banana');
