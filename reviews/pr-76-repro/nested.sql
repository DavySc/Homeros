select *
from t
where exists
(
select 1
from u
where u.a in
(
select b
from v
)
and u.c = 1
)
and t.d = 2
