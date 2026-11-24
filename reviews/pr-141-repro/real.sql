SET NOCOUNT ON;

DECLARE @start_date date = '2026-01-01', @batch_size int = 500;
DECLARE @ids TABLE (order_id int, customer_id int);

CREATE TABLE #staging (
    order_id    int NOT NULL,
    customer_id int NOT NULL,
    total       decimal(18, 2),
    [order date] datetime2,
    CONSTRAINT pk_staging PRIMARY KEY (order_id)
);

WITH recent AS (
    SELECT o.order_id, o.customer_id, o.total
    FROM   dbo.orders AS o
    WHERE  o.created_at >= @start_date
)
INSERT INTO #staging (order_id, customer_id, total)
SELECT r.order_id, r.customer_id, r.total
FROM   recent r
       join dbo.customers c ON c.customer_id = r.customer_id
WHERE  c.is_active = 1;

IF EXISTS (SELECT 1 FROM #staging s WHERE s.total < 0)
BEGIN
    UPDATE s
    SET    s.total = 0
    FROM   #staging s
    WHERE  s.total < 0;
END

SELECT c.name, COUNT(*) AS n
FROM   dbo.customers c
       join #staging st ON st.customer_id = c.customer_id
GROUP BY c.name;
