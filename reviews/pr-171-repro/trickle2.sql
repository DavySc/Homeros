select g, repeat('x', 3000) as pad, pg_sleep(0.02) from generate_series(1,100) g
