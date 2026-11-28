declare @n nvarchar(200) = replicate(nchar(0x4e2d), 128);
declare @s nvarchar(max) = N'create database ' + quotename(@n) + N' on (name = l, filename = ''/var/opt/mssql/data/long.mdf'') log on (name = ll, filename = ''/var/opt/mssql/data/long.ldf'')';
if db_id(@n) is null exec(@s);
go
reconfigure;
go
if db_id('hct') is null create database hct containment = partial;
go
select name, collation_name, containment from sys.databases;
