merge dom as t using src as s on t.id = s.id when matched then update set name = s.name;
select * from app a join dom d on d|
