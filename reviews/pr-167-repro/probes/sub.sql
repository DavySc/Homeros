select name from dom where id in (select dom_id from app)
select * from app a join dom d on d|
