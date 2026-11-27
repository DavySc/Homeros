select name, kind from dbo.dom where born > getdate() and name like N'a%' collate Latin1_General_CI_AS
select * from app a join dom d on d|
