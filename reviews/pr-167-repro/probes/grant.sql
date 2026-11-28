grant select on dom to reader
if exists (select 1 from dom where gone = 0) print 'x'
select top (10) percent name into #t from dom
select * from app a join dom d on d|
