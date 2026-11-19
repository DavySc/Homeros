with a as
(
select x from t
),
b as
(
select y from a
where y in
(
select 1
)
)
select * from b
