select top 10000 a.*, b.* from sys.all_columns a cross join (select top 1 * from sys.all_objects) b
