set nocount on
declare @i int = 0
while @i < 20000 begin select @i as i; set @i += 1 end
