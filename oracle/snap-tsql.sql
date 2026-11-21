-- One line per catalog fact, tab-separated, with \ tab LF CR escaped by the
-- server, so the editor reads it without a SQL client library.  Catalog
-- names and module text can carry different collations (msg 451), so
-- every string is cast to the database's.
set nocount on;
select concat_ws(char(9), 'D', convert(nvarchar(128), databasepropertyex(db_name(), 'Collation')), schema_name())
union all
select concat_ws(char(9), 'O', o.object_id, s.name collate database_default, o.name collate database_default, rtrim(o.type) collate database_default)
from sys.objects o join sys.schemas s on s.schema_id = o.schema_id
where o.type in ('U','V','IF','TF','FN','P','SN','TR')
union all
-- a table type by its type's name (its sys.objects row is TT_name_hex)
select concat_ws(char(9), 'O', t.type_table_object_id, s.name collate database_default, t.name collate database_default, 'TT')
from sys.table_types t join sys.schemas s on s.schema_id = t.schema_id
union all
select concat_ws(char(9), 'C', c.object_id, c.column_id, c.name collate database_default, type_name(c.user_type_id) collate database_default, c.max_length, c.is_nullable)
from sys.columns c join sys.objects o on o.object_id = c.object_id
where o.type in ('U','V','IF','TF','TT')
union all
select concat_ws(char(9), 'K', f.constraint_object_id, f.constraint_column_id, f.parent_object_id, f.parent_column_id, f.referenced_object_id, f.referenced_column_id)
from sys.foreign_key_columns f
union all
-- why sp_rename refuses a column (measured, KB 03): it is computed or
-- ROWGUIDCOL (4928) or a column set (11408); the clustered key under an
-- XML or spatial index (5074);
-- something schema-bound uses it (15336: a computed column, a CHECK, a
-- filtered index, a SCHEMABINDING module); a history table's (13xxx: the
-- current table's rename carries it, H)
select concat_ws(char(9), 'X', object_id, column_id, case when is_computed = 1 then 'computed (4928)' when is_rowguidcol = 1 then 'ROWGUIDCOL (4928)' else 'a column set (11408)' end)
from sys.columns where is_computed = 1 or is_rowguidcol = 1 or is_column_set = 1
union all
select concat_ws(char(9), 'X', referenced_id, referenced_minor_id, string_agg(convert(nvarchar(max), why), ', '))
from (select distinct d.referenced_id, d.referenced_minor_id, concat(
  case when d.referencing_class = 7 then 'filtered index ' + i.name
       when o.type = 'U' then 'computed column ' + col_name(d.referencing_id, d.referencing_minor_id)
       else lower(replace(o.type_desc, '_', ' ')) + ' ' + schema_name(o.schema_id) + '.' + o.name end, ' (15336)') collate database_default why
  from sys.sql_expression_dependencies d
  left join sys.objects o on d.referencing_class = 1 and o.object_id = d.referencing_id
  left join sys.indexes i on d.referencing_class = 7 and i.object_id = d.referencing_id and i.index_id = d.referencing_minor_id
  where d.is_schema_bound_reference = 1 and d.referenced_minor_id > 0 and d.referenced_id is not null) x
group by referenced_id, referenced_minor_id
union all
-- an XML or spatial index needs the clustered key: its columns stay (5074)
select concat_ws(char(9), 'X', k.object_id, k.column_id, min(x.type_desc collate database_default + ' index ' + x.name collate database_default) + ' (5074)')
from sys.indexes x join sys.indexes c on c.object_id = x.object_id and c.index_id = 1
join sys.index_columns k on k.object_id = c.object_id and k.index_id = 1 and k.key_ordinal > 0
where x.type in (3, 4)
group by k.object_id, k.column_id
union all
select concat_ws(char(9), 'H', object_id, history_table_id)
from sys.tables where history_table_id is not null
union all
select concat_ws(char(9), 'M', m.object_id,
  replace(replace(replace(replace(m.definition collate database_default, '\', '\\'), char(9), '\t'), char(10), '\n'), char(13), '\r'))
from sys.sql_modules m join sys.objects o on o.object_id = m.object_id
where o.type in ('V','IF','TF','FN','P','TR');   -- not DDL triggers (no sys.objects row), defaults, checks
