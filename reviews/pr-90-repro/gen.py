#!/usr/bin/env python3
"""gen.py DIR: the texts the report's probes ran on."""
import json, random, sys
d = sys.argv[1] + "/"
open(d + "big.sql", "w").write("".join("select col%d, other from dbo.table_%d where id = %d and name like x;\n" % (i, i, i) for i in range(10000)))   # 746,670 bytes
open(d + "long.txt", "w").write("ab" * 150000 + "\n")
open(d + "aaa.txt", "w").write(("a" * 60 + "\n") * 60)
open(d + "wide.txt", "w").write(("x" * 20000 + " foo\n") * 60)
open(d + "w2.txt", "w").write(("x" * 3000 + " foo\n") * 3)
random.seed(1)
open(d + "min.json", "w").write(json.dumps([{"id": i, "name": "item%d" % i, "tags": ["a", "b", "c"], "value": random.random()} for i in range(400)], separators=(",", ":")) + "\n")
open(d + "t3.txt", "w").write("foo bar foo\nabc Foo x1 22\n  foo.bar(a) 12\nbaz? aXa bXb\n\nx/y?z foo\nfoofoo zz\nend of it\n")
open(d + "t4.txt", "w").write("ab\ncd\nzz\n")
open(d + "a.txt", "w").write("one two\nfoo boo\nlong long line three o o o o\nfour o\n")
open(d + "b.txt", "w").write("x\ny\n")
