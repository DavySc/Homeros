create procedure dbo.get_dom @k int, @n varchar(20) = null
as
begin
    set nocount on;
    select name from dom where kind = @k;
end
go
select * from app a join dom d on d|
