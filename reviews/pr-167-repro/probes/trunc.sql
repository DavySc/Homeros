truncate table dom
drop table dom
alter table dom add born date null
create index ix on dom (name, kind)
select * from app a join dom d on d|
