select case when kind = 1 then name else descr end as label, x.y from dom x order by born desc offset 10 rows fetch next 5 rows only
select * from app a join dom d on d|
