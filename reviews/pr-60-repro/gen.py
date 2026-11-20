# writes the large inputs of the review into build/rv
rows = ",\n".join("(%d, 'name%d', %d.5, null)" % (i, i, i) for i in range(9000))
open('build/rv/bigvalues.sql', 'w').write("INSERT INTO dbo.t (a, b, c, d) VALUES\n" + rows + "\nSELECT a FROM dbo.t WHERE a = 1;\n")
for k in (32767, 32768):
    open('build/rv/tokens%d.sql' % (2 * k + 2), 'w').write('SELECT ' + ','.join(['a'] * k) + ' FROM t\n')
for k in (5000, 20000):
    open('build/rv/comments%d.sql' % k, 'w').write("select 1;\n" + "-- a commented-out line of code\n" * k + "select 2;\n")
open('build/rv/nul-byte.sql', 'wb').write(b'SELECT 1\0SELECT 2\n')
