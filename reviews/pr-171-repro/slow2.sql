select 1 as a
raiserror('progress 1', 0, 1) with nowait
waitfor delay '00:00:02'
select 2 as b
