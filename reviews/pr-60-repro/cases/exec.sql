EXEC sp_executesql N'SELECT 1', N'@a INT', @a=1
EXEC('select 1')
INSERT t EXEC p
INSERT INTO t EXECUTE p @a
