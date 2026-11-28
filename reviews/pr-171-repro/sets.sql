print 'before'
select 1 as a, 2 as b
select top 0 name, object_id from sys.objects
select null as onlynull
set nocount on
select 3 as c
select 4 as d
raiserror('boom %d', 16, 1, 7)
select * from nosuchtable
go
select 'batch2' as x
