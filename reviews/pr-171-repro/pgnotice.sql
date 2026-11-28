create or replace function pg_temp.n(i int) returns int as $$ begin raise notice 'row %', i; return i; end $$ language plpgsql;
select pg_temp.n(g) as v from generate_series(1, 5) g;
