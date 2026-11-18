:setvar x 1
SELECT $(x)
:r file.sql
  :on error exit
SELECT 1 :foo
