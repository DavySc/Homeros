exec sp_help dom
delete from dom where id = 3
declare @x int = (select max(id) from dom)
select * from app a join dom d on d|
