-- Hand-written queries over WWI that the generator does not make, each
-- checked like a generated one (oracle/gen_oracle.py tests/queries.sql):
-- the server's verdict, result columns and referenced columns.  One per
-- batch; the invalid ones are meant to be.
with n(i) as (select 1 union all select i + 1 from n where i < 5) select i from n
go
with org as (select PersonID, FullName, 0 as lvl from Application.People where PersonID = 1 union all select p.PersonID, p.FullName, o.lvl + 1 from Application.People p join org o on p.PersonID = o.PersonID + 100) select * from org
go
select v.a, v.b from (values (1, 2), (3, 4)) v(a, b)
go
select c.CustomerName, o.OrderID from (Sales.Customers c join Sales.Orders o on o.CustomerID = c.CustomerID) where c.CustomerID = 1
go
select OrderID from Sales.Orders order by OrderID offset 10 rows fetch next 5 rows only
go
select OrderID, CustomerID from Sales.Orders where OrderID < 3 for json path
go
select top (5) with ties OrderID, OrderDate from Sales.Orders order by OrderDate
go
select Sales.Orders.OrderID, Orders.CustomerID from Sales.Orders
go
select o.OrderID from Sales.Orders o where Orders.OrderID = 1
go
select case when o.OrderID > 1 then 'a' else 'b' end as k, o.OrderID from Sales.Orders o where case o.CustomerID when 1 then 1 else 0 end = 1
go
select l.OrderLineID, x.n from Sales.OrderLines l cross apply (select count(*) n from Sales.Orders o where o.OrderID = l.OrderID) x
go
select l.OrderLineID from Sales.OrderLines l join Sales.Orders o on o.OrderID = l.OrderID and exists (select 1 from Sales.Customers c where c.CustomerID = o.CustomerID)
go
select OrderID from Sales.Orders union select InvoiceID from Sales.Invoices order by OrderID
go
select OrderID from Sales.Orders union select InvoiceID from Sales.Invoices order by InvoiceID
go
select OrderID as x from Sales.Orders order by x
go
select OrderID as x from Sales.Orders where x = 1
go
select c.CustomerID, c.CustomerName from Sales.Customers as c where c.CustomerID in (select CustomerID from Sales.Orders where OrderID = c.CustomerID)
go
select s.value from string_split('a,b', ',') s
go
select j.[key], j.value from openjson('{"a":1}') j
go
select j.id from openjson('[{"id":1}]') with (id int '$.id', nm nvarchar(10)) j
go
select OrderID, row_number() over (partition by CustomerID order by OrderDate desc) rn, sum(OrderID) over (order by OrderID rows between unbounded preceding and current row) s from Sales.Orders
go
select convert(varchar(10), OrderDate, 120) d, cast(OrderID as decimal(10, 2)) e, datediff(day, OrderDate, ExpectedDeliveryDate) f, try_convert(int, Comments) g from Sales.Orders
go
select p.PersonID from Application.People p where p.FullName collate Latin1_General_BIN = N'x'
go
select CityID from Application.Cities for system_time as of '2016-01-01' c
go
select c.CityID from Application.Cities for system_time all as c
go
select OrderID, next value for Sequences.OrderID from Sales.Orders
go
select [OrderID] from [Sales].[Orders] [o] where [o].[CustomerID] = 1
go
select OrderID from Sales.Orders o join Sales.OrderLines l on l.OrderID = o.OrderID
go
select CustomerID from Sales.Customers c, Sales.Orders o where c.CustomerID = o.CustomerID
go
select a.OrderID from Sales.Orders a, Sales.OrderLines b join Sales.Invoices i on i.OrderID = a.OrderID
go
select x.OrderID from (select OrderID, OrderID from Sales.Orders) x
go
select x.k from (select count(*) from Sales.Orders) x(k)
go
select * from (select count(*) from Sales.Orders) x
go
with c(a, b) as (select OrderID from Sales.Orders) select a from c
go
select p.[1], p.[2] from (select CustomerID, BillToCustomerID from Sales.Customers) s pivot (count(BillToCustomerID) for CustomerID in ([1], [2])) p
go
select u.v, u.col from (select CustomerID, BillToCustomerID from Sales.Customers) s unpivot (v for col in (CustomerID, BillToCustomerID)) u
go
select o.OrderID from Sales.Orders o where o.OrderID = (select max(OrderID) from Sales.Orders where CustomerID = o.CustomerID)
go
select distinct top 3 o.CustomerID from Sales.Orders o group by o.CustomerID having count(*) > 1 order by o.CustomerID
go
select (select top 1 c.CustomerName from Sales.Customers c where c.CustomerID = o.CustomerID) nm from Sales.Orders o
go
select OrderID from Sales.Orders with (nolock) where OrderID = 1
go
select o.OrderID from Sales.Orders o tablesample (10 percent)
go
select x.OrderID from Sales.Orders x join Sales.Orders x on 1 = 1
go
select OrderID from Sales.Orders join Sales.Orders on 1 = 1
go
select Comments from Sales.Orders o join Sales.Invoices i on i.OrderID = o.OrderID
go
select zz from Sales.Orders
go
select OrderID from Sales.Nope
go
select [Order ID] = OrderID, 'x' as 'lit' from Sales.Orders
go
select o.OrderID from Sales.Orders o where o.OrderID in (select l.OrderID from Sales.OrderLines l where l.OrderID = o.OrderID and l.Quantity > (select avg(Quantity) from Sales.OrderLines))
go
select r.CustomerID from Sales.Orders r intersect select CustomerID from Sales.Customers
go
select 1 as a, 2 as a from Sales.Orders order by a
go
select StockItemID from Warehouse.StockItems where StockItemName like N'%x%' escape '!'
go
select t.TransactionAmount from Sales.CustomerTransactions t where t.TransactionDate between '2016-01-01' and '2016-02-01'
go
select o.OrderID from Sales.Orders o where cast(o.OrderDate as datetime2) at time zone 'UTC' > '2016-01-01'
go
select string_agg(o.Comments, ',') within group (order by o.OrderID) from Sales.Orders o
go
select iif(o.OrderID > 1, o.CustomerID, null) from Sales.Orders o
go
select o.OrderID from Sales.Orders o order by o.OrderDate, CustomerID desc
go
select * from Application.People p cross apply (select top 1 c.CityName from Application.Cities c where c.LastEditedBy = p.PersonID) c2
go
select * from Website.Customers
go
with a as (select 1 x), b as (select x from a) select b.x from b join a on a.x = b.x
go
with a as (select 1 x) select a.x from a a
go
select 1 from Sales.Orders o, Sales.Customers o
go
-- DML is slice 4: until then only the verdict is compared, and a name the
-- unread UPDATE explains must not be reported missing
update o set Comments = (select top 1 c.CustomerName from Sales.Customers c where c.CustomerID = OrderID) from Sales.Orders o where 1 = 0
go
delete o from Sales.Orders o where o.OrderID = (select max(l.OrderID) from Sales.OrderLines l where l.OrderLineID = CustomerID) and 1 = 0
go
-- slice 3: star and qualify give exactly this
-- star: select o.OrderID, o.CustomerID, c.CustomerID, c.CustomerName from (select OrderID, CustomerID from Sales.Orders) o join (select CustomerID, CustomerName from Sales.Customers) c on c.CustomerID = o.CustomerID
-- qualify: select * from (select Orders.OrderID, Orders.CustomerID from Sales.Orders) o join (select Customers.CustomerID, Customers.CustomerName from Sales.Customers) c on c.CustomerID = o.CustomerID
select * from (select OrderID, CustomerID from Sales.Orders) o join (select CustomerID, CustomerName from Sales.Customers) c on c.CustomerID = o.CustomerID
go
-- names that must be bracketed: reserved, space, ], leading digit
-- star: select d.[select], d.[a b], d.[x]]y], d.[1x], d.ok from (select 1 as [select], 2 as [a b], 3 as [x]]y], 4 [1x], 5 ok) d
select * from (select 1 as [select], 2 as [a b], 3 as [x]]y], 4 [1x], 5 ok) d
go
-- EXISTS's * stays (the server ignores its list); the outer one is
-- written out; qualify adds the exposed name, which is the alias
-- star: select c.CityID, c.CityName from (select CityID, CityName from Application.Cities) c where exists (select * from Application.StateProvinces s where s.StateProvinceID = CityID)
-- qualify: select * from (select Cities.CityID, Cities.CityName from Application.Cities) c where exists (select * from Application.StateProvinces s where s.StateProvinceID = c.CityID)
select * from (select CityID, CityName from Application.Cities) c where exists (select * from Application.StateProvinces s where s.StateProvinceID = CityID)
go
-- x.* only expands x; an unaliased table's exposed name is its last part
-- star: select CityID, s.StateProvinceID from Application.Cities join (select StateProvinceID from Application.StateProvinces) s on s.StateProvinceID = Cities.StateProvinceID
-- qualify: select Cities.CityID, s.* from Application.Cities join (select StateProvinces.StateProvinceID from Application.StateProvinces) s on s.StateProvinceID = Cities.StateProvinceID
select CityID, s.* from Application.Cities join (select StateProvinceID from Application.StateProvinces) s on s.StateProvinceID = Cities.StateProvinceID
go
-- the inner x0 hides the outer one: qualifying d would change its meaning
-- qualify: REFUSED
select (select count(*) from Sales.Customers x0 where x0.CustomerID = OrderID) from Sales.Orders x0
go
-- OPENJSON with no alias cannot be named: its columns are left as they are
-- qualify: select v from openjson(N'[{"v":1}]') with (v int)
select v from openjson(N'[{"v":1}]') with (v int)
go
-- info: a column through a derived table, with its foreign keys; a table
-- info CustomerID: Sales.Orders.CustomerID int not null
-- info Orders: Sales.Orders (U)
-- info cnt: REFUSED: a computed column
select d.CustomerID, d.cnt from (select CustomerID, count(*) cnt from Sales.Orders group by CustomerID) d
go
-- a column that may come from a source of unknown columns is left alone
-- qualify: select Cities.CityName, (select top 1 name from sys.objects) from Application.Cities
select CityName, (select top 1 name from sys.objects) from Application.Cities
go
-- PIVOT's columns are not known yet: its * stays
-- star: REFUSED
select * from (select CityID, StateProvinceID, LastEditedBy from Application.Cities) s pivot (count(CityID) for StateProvinceID in ([1], [2])) p
go
-- slice 4: DML.  The target named by the table when FROM has it once with an alias
update Sales.Orders set Comments = c.CustomerName from Sales.Orders o join Sales.Customers c on c.CustomerID = o.CustomerID where 1 = 0
go
-- the target not in FROM is a source of its own; a bare column on the left is the target's
-- qualify: update Sales.Orders set Comments = Customers.CustomerName from Sales.Customers where Orders.CustomerID = Customers.CustomerID and 1 = 0
update Sales.Orders set Comments = CustomerName from Sales.Customers where Orders.CustomerID = Customers.CustomerID and 1 = 0
go
-- two aliased references and none bare: Msg 8154
update Sales.Orders set Comments = 'x' from Sales.Orders a, Sales.Orders b where 1 = 0
go
-- the same with a bare reference: that one is the target, no error
update Sales.Orders set Comments = 'x' from Sales.Orders a, Sales.Orders b, Sales.Orders where 1 = 0
go
-- a column set twice: Msg 264
update Sales.Orders set Comments = 'x', comments = 'y' where 1 = 0
go
-- INSERT's query does not see the target
insert Application.Cities (CityID, CityName) select CityID, StateProvinceName from Application.StateProvinces where 1 = 0
go
insert Application.Cities (CityID, CityName) select CityID, CityName from Application.StateProvinces where 1 = 0
go
-- OUTPUT: inserted and deleted are the target's; FROM's sources too
update o set Comments = 'x' output inserted.Comments, deleted.OrderID, c.CustomerName from Sales.Orders o join Sales.Customers c on c.CustomerID = o.CustomerID where 1 = 0
go
-- MERGE: NOT MATCHED sees the source only, BY SOURCE the target only
merge Sales.Orders t using Sales.Customers s on t.CustomerID = s.CustomerID when not matched by source and t.OrderID < 0 then delete when matched and 1 = 0 then update set Comments = s.CustomerName;
go
merge Sales.Orders t using Sales.Customers s on t.CustomerID = s.CustomerID when not matched by source and CustomerName = 'x' then delete;
go
-- a temp table and a table variable made in the text; star and qualify through them
-- star: create table #t (k int, v nvarchar(10)); insert #t (k) select CityID from Application.Cities where 1 = 0; select #t.k, #t.v from #t
-- qualify: create table #t (k int, v nvarchar(10)); insert #t (k) select Cities.CityID from Application.Cities where 1 = 0; select * from #t
create table #t (k int, v nvarchar(10)); insert #t (k) select CityID from Application.Cities where 1 = 0; select * from #t
go
-- a table variable with no alias has no name a qualifier can use: left as it is
-- qualify: declare @t table (k int primary key, v int); select k from @t
declare @t table (k int primary key, v int); select k from @t
go
-- qualify: declare @t table (k int primary key, v int); select x.k from @t x
declare @t table (k int primary key, v int); select k from @t x
go
-- SELECT INTO a temp table: its columns are the select list's
-- star: select CityID, CityName into #c from Application.Cities where 1 = 0; select #c.CityID, #c.CityName from #c
select CityID, CityName into #c from Application.Cities where 1 = 0; select * from #c
go
select CityID into #c from Application.Cities where 1 = 0; select CityName from #c
go
-- a MERGE search condition naming what its clause cannot see: 5333, 5334
merge Sales.Orders t using Sales.Customers s on t.CustomerID = s.CustomerID when not matched and t.OrderID = 1 then insert (CustomerID) values (s.CustomerID);
go
merge Sales.Orders t using Sales.Customers s on t.CustomerID = s.CustomerID when not matched by source and zz = 1 then delete;
go
-- $action and the target's columns in OUTPUT
merge Sales.Orders t using Sales.Customers s on t.CustomerID = s.CustomerID when matched and 1 = 0 then update set Comments = s.CustomerName output $action, inserted.OrderID, s.CustomerName;
go
-- a table variable's columns cannot be named through @t (Msg 137)
declare @t table (k int); select @t.k from @t
go
-- a table-level constraint is no column; CURRENT_USER is no column
-- star: declare @t table (k int, v int, primary key (k), unique (v)); select x.k, x.v, current_user from @t x
declare @t table (k int, v int, primary key (k), unique (v)); select *, current_user from @t x
go
