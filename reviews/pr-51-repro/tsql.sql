-- T-SQL as SSMS users write it
USE [AdventureWorks];
GO
DECLARE @id INT = 42, @name NVARCHAR(50) = N'O''Brien';
SET NOCOUNT ON;
/* block comment /* nested */ still */ 
SELECT TOP (10) [o].[OrderID], o.CustomerID AS [Customer Id], COUNT(*) OVER (PARTITION BY o.CustomerID) AS n
FROM [dbo].[Orders] AS o WITH (NOLOCK)
INNER JOIN dbo.Customers c ON c.CustomerID = o.CustomerID
WHERE o.OrderDate >= '2024-01-01' AND c.Name LIKE @name + '%' AND o.Total BETWEEN 1.5 AND 2e3
ORDER BY o.OrderDate DESC;
IF @@ROWCOUNT > 0 BEGIN PRINT 'rows'; END ELSE BEGIN RAISERROR('none', 16, 1); END
CREATE TABLE #tmp (id INT IDENTITY(1,1) PRIMARY KEY, v VARCHAR(MAX) NULL, d DATETIME2 DEFAULT GETDATE());
INSERT INTO #tmp (v) VALUES ('a'), ("b"), (NULL);
UPDATE t SET t.v = UPPER(t.v) FROM #tmp t WHERE t.id IN (SELECT id FROM #tmp);
EXEC sp_executesql N'SELECT @x', N'@x int', @x = 1;
BEGIN TRY BEGIN TRANSACTION; COMMIT; END TRY BEGIN CATCH ROLLBACK; THROW; END CATCH
CREATE OR ALTER PROCEDURE dbo.p @a INT, @b INT OUTPUT AS BEGIN SET @b = @a * 2; RETURN 0; END
GO
WITH cte AS (SELECT 1 AS n UNION ALL SELECT n + 1 FROM cte WHERE n < 10) SELECT * FROM cte OPTION (MAXRECURSION 100);
MERGE INTO t USING s ON t.id = s.id WHEN MATCHED THEN UPDATE SET v = s.v WHEN NOT MATCHED THEN INSERT (v) VALUES (s.v);
SELECT CASE WHEN x IS NULL THEN 0 ELSE CAST(x AS BIGINT) END, 'it''s', 'multi
line string', "dq ""quoted""", [we]]ird], 0x1F, -1, TRUE, false FROM t;
