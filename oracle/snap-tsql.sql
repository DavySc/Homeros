-- One line per catalog fact, tab-separated, with \ tab LF CR escaped by the
-- server, so the editor reads it without a SQL client library.  Catalog
-- names and module text can carry different collations (msg 451), so
-- every string is cast to the database's.
set nocount on;
select concat_ws(char(9), 'D', convert(nvarchar(128), databasepropertyex(db_name(), 'Collation')), schema_name())
union all
select concat_ws(char(9), 'O', o.object_id, s.name collate database_default, o.name collate database_default, rtrim(o.type) collate database_default)
from sys.objects o join sys.schemas s on s.schema_id = o.schema_id
where o.type in ('U','V','IF','TF','FN','P','SN','TT','TR')
union all
select concat_ws(char(9), 'C', c.object_id, c.column_id, c.name collate database_default, type_name(c.user_type_id) collate database_default, c.max_length, c.is_nullable)
from sys.columns c join sys.objects o on o.object_id = c.object_id
where o.type in ('U','V','IF','TF')
union all
select concat_ws(char(9), 'K', f.constraint_object_id, f.constraint_column_id, f.parent_object_id, f.parent_column_id, f.referenced_object_id, f.referenced_column_id)
from sys.foreign_key_columns f
union all
select concat_ws(char(9), 'M', m.object_id,
  replace(replace(replace(replace(m.definition collate database_default, '\', '\\'), char(9), '\t'), char(10), '\n'), char(13), '\r'))
from sys.sql_modules m join sys.objects o on o.object_id = m.object_id
where o.type in ('V','IF','TF','FN','P','TR');   -- not DDL triggers (no sys.objects row), defaults, checks
