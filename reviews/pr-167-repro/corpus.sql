SET NOCOUNT ON;
DECLARE @today date = CAST(GETDATE() AS date), @limit int = 100;

SELECT order_id, customer_id, total, created_at
FROM   dbo.orders WITH (NOLOCK)
WHERE  created_at >= DATEADD(day, -7, @today)
       AND status IN ('open', 'held')
ORDER BY created_at DESC;

SELECT TOP (10) name, email, ISNULL(phone, '') AS phone
FROM   dbo.customers
WHERE  is_active = 1 AND name LIKE @q + '%'
ORDER BY name;

SELECT customer_id, COUNT(*) AS n, SUM(total) AS revenue, MAX(created_at) last_order
FROM   dbo.orders
GROUP BY customer_id
HAVING SUM(total) > 1000;

SELECT order_id,
       CONVERT(varchar(10), created_at, 120) AS day,
       CAST(total AS decimal(18, 2)) AS total,
       ROW_NUMBER() OVER (PARTITION BY customer_id ORDER BY created_at DESC) AS rn,
       CASE WHEN total > 500 THEN 'big' ELSE 'small' END AS size
FROM   dbo.orders;

UPDATE dbo.orders
SET    status = 'closed', closed_at = SYSDATETIME()
WHERE  order_id = @id;

DELETE FROM dbo.order_lines WHERE order_id = @id AND qty = 0;

INSERT INTO dbo.audit (who, what, at) VALUES (SUSER_SNAME(), 'close', GETDATE());

SELECT line_no, sku, qty, price, qty * price AS amount
FROM   dbo.order_lines
WHERE  order_id = @id
ORDER BY line_no
OFFSET 0 ROWS FETCH NEXT @limit ROWS ONLY;

SELECT DISTINCT sku FROM dbo.order_lines WHERE price BETWEEN 10 AND 20 OPTION (RECOMPILE);

SELECT name, COALESCE(nickname, name) shown, LEN(name) AS len, UPPER(LEFT(name, 1)) initial
FROM   dbo.customers
WHERE  created_at < @today AND email IS NOT NULL AND country = N'NL' COLLATE Latin1_General_CI_AS;

IF EXISTS (SELECT 1 FROM dbo.orders WHERE status = 'held' AND held_until < GETDATE())
    PRINT 'late';

SELECT @n = COUNT(*) FROM dbo.customers WHERE TRY_CONVERT(int, postcode) IS NULL;

SELECT sku, STRING_AGG(CAST(qty AS varchar(max)), ',') WITHIN GROUP (ORDER BY line_no) AS qtys
FROM   dbo.order_lines
GROUP BY sku
FOR JSON PATH;

SELECT o.order_id, c.name
FROM   dbo.orders o
       JOIN dbo.customers c ON c.customer_id = o.customer_id
WHERE  o.total > 0;
