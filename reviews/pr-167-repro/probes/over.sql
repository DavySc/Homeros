select name, row_number() over (partition by kind order by born desc) rn from dom
select * from app a join dom d on d|
