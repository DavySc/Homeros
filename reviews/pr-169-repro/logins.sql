declare @t nvarchar(max) = N'';
declare @c table(n sysname, p nvarchar(128));
insert @c values (N'h_sp', N'Ho meros 1 x'), (N'h_trail', N'trail  '), (N'h_tab', N'a' + char(9) + N'b c'), (N'h_hash', N'a #b c'), (N'h_pt', N'x trust'), (N'h_pass', N'a pass=b'), (N'h_lead', N' lead');
select @t += N'if suser_id(''' + n + N''') is not null drop login ' + n + N'; create login ' + n + N' with password = ''' + p + N''', check_policy = off;' from @c;
exec(@t);
select name from sys.sql_logins where name like 'h[_]%';
