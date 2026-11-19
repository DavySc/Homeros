-- A case-sensitive database with names that break naive lookups: case
-- twins, brackets, spaces, a ] inside a name, the same name in two schemas,
-- a column dropped (id gap), non-ASCII.  Test servers only.
use master;
if db_id('tricky') is not null drop database tricky;
create database tricky collate Latin1_General_CS_AS;
go
use tricky;
go
create schema s1;
go
create schema s2;
go
create table dbo.T (a int primary key, A2 int, [b c] int, [x]]y] int, [ünï] int);
create table dbo.t (a int, [A] int);
create table s1.only1 (z int);
create table s1.twin (z int);
create table s2.twin (z int, gone int, w int references dbo.T(a));
alter table s2.twin drop column gone;
go
create view s1.v as select a, [b c] from dbo.T;
go
create proc dbo.[p q] as select 'tab	inside' as x, N'ünï\n' as [y\] -- a comment with \ and a backslash-n \n
go
