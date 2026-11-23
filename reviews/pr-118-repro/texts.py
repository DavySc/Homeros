# directed texts for the review plants no check caught: python3 texts.py DIR
import os, sys
T = {
    'ml1': '[[a\nb][c]] x\n', 'ml2': 'x [[a\nb c][d\ne]] y\n', 'ml3': '[[a][b\nc]] x^{2}\n',
    'dd1': '[fn:1] a\n$$x [fn:1]$$ y\n', 'dd2': '[fn:1] a\n$$ [fn:1]\n', 'dd3': '[fn:1] a\n$$[fn:1]$ $\n',
    've1': '[fn:1] a\n== [fn:1] b=\n', 've2': '[fn:1] a\nx == [fn:1] =\n', 've3': '[fn:1] a\n=a=\n[fn:1] x\n',
    've4': '[fn:1] a\nb =c\nd=\n[fn:1] x\n', 've5': '[fn:1] a\nb =c\nd= e\n[fn:1] x\n', 've6': '[fn:1] a\n=c=\n\n[fn:1] x\n',
    've7': '[fn:1] a\n=c=x\n[fn:1] =y=\n', 've8': '[fn:1] a\n=c= =d\n[fn:1] y= z\n',
    'ho1': '[fn:1] a\nx [[a]][fn:1]\n', 'ho2': '[fn:1] a\n\\cite{a}[fn:1]\n', 'ho3': '[fn:1] a\n\\cite{a[fn:1]}\n',
    'ho4': 'x [[a][fn:1]]\n', 'ho5': 'x [[a]] [fn:1]\n',
    'sp1': 'x [fn:1:a\nb] c [fn:2:d\ne] f\ng\n', 'sp2': '[fn:1] d\nx [fn:2:a\n[fn:1] b] c\n d\n',
    'sp3': '[fn:9] d\n\nx [fn:1:a\ny [fn:9] z\nw] v\nu\n', 'sp4': '[fn:9] d\nx [fn:: a\n[fn:9] [fn:: b\nc\nd] e\nf] g\nh\n',
    'sc1': '[fn:1] a\n$;[fn:1]$ x\n', 'sc2': '[fn:1] a\n$;x$ [fn:1]\n', 'st1': '[fn:1] a\nx \\cite*{[fn:1]}\n', 'st2': '[fn:1] a\nx \\cite*[b]{[fn:1]} y\n',
    'ff1': 'x http://ab\x0c y\n', 'ff2': 'http://a\x0c\n',
    'dd4': '[fn:1] a\n$$x$$ [[fn:1]]\n', 'dd5': '[fn:1] a\n$$x$$ \\cite{[fn:1]}\n', 'sc3': '[fn:1] a\n$;x$ [[fn:1]]\n', 'sc4': '[fn:1] a\n$;x$ \\cite{[fn:1]}\n',
    'ho6': '[fn:1] d\nx \\a{[fn:1}] y\n', 've9': '[fn:1] a\n== [[fn:1]] b\n', 've10': '[fn:1] a\n== \\cite{[fn:1]} b\n',
    'kw1': '[[x\n#+a[b c]: d]]\n', 'kw2': '<id:x\n#+ab[ c]:>\n', 'fw1': '[fn:1] a\n: [[fn:1]]\n', 'fw2': '[fn:1] a\n : \\cite{[fn:1]}\n',
    'ty1': 'x\nhttp:ab c\nfile+emacs:ab\n', 'ty2': 'x shortdoc:ab\nfile+sys:cd\n',
}
os.makedirs(sys.argv[1], exist_ok=True)
for k, v in T.items():
    open(os.path.join(sys.argv[1], k + '.org'), 'wb').write(v.encode())
