select E'\375abc'::text as v, E'caf\351'::text as latin1, 'x' as ok;
select E'a\377b'::text as v2;
select E'\376\377'::text as "n";
