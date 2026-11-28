if db_id('my db') is null create database [my db];
if db_id('hjp') is null create database hjp collate Japanese_CS_AS;
if db_id('hcs2') is null create database hcs2 collate Latin1_General_BIN2;
exec sp_configure 'contained database authentication', 1; reconfigure;
if db_id('hct') is null create database hct containment = partial;
declare @n nvarchar(200) = replicate(nchar(0x4e2d), 128);
declare @s nvarchar(400) = N'create database ' + quotename(@n);
if db_id(@n) is null exec(@s);
select name, collation_name from sys.databases;
