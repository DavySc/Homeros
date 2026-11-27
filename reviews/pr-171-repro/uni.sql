select N'日本語のテキスト' as [漢字], N'e' + nchar(769) + N'cole' as combining, N'👨‍👩‍👧 fam' as emoji, N'Ünïcödé' as latin, cast(null as int) as n, '' as empty, 'a	b' as tab, 'line1
line2' as nl, replicate('x', 45) as long45, replicate(N'語', 30) as wide30, 'NULL' as fakenull, 1, cast(0x00FCFDFEFF as varbinary(8)) as bin, char(7) + 'bell' as ctl
union all select N'短', N'x', N'🙂', N'y', 5, 'z', 't', 'n', 'short', N'語', null, 2, null, 'c'
