CREATE OR REPLACE FUNCTION add(a integer, b integer) RETURNS integer AS $fn$
BEGIN
  -- comment in body with $$ and 'quote
  RETURN a + b; /* $fn */
END;
$fn$ LANGUAGE plpgsql;
DO $$ DECLARE r record; BEGIN FOR r IN SELECT $q$x$q$ AS s LOOP RAISE NOTICE '%', r.s; END LOOP; END $$;
SELECT $$ plain $$, $a$ in $b$ nested $b$ a $a$, E'esc\n', $1, $2::text, x->>'k', arr[1:2], 'é'::text, "Ünï";
CREATE FUNCTION f() RETURNS int AS $$ SELECT $$;
CREATE FUNCTION g() RETURNS int AS $$ SELECT 1 $$ LANGUAGE sql; -- TODO: note
COPY t FROM stdin;
SELECT $never$ closed
