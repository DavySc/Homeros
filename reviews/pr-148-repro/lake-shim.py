#!/usr/bin/env python3
"""A `lake` that runs the real one and logs each LSP message's time,
direction, id and method to $SHIM_LOG (token answers with their count)."""
import json, os, subprocess, sys, threading, time
real = os.environ['REAL_LAKE']; log = open(os.environ['SHIM_LOG'], 'a'); t0 = time.time()
p = subprocess.Popen([real] + sys.argv[1:], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
lock = threading.Lock()
def pump(src, dst, tag):
    while True:
        n = 0
        while True:
            h = src.readline()
            if not h: dst.close(); return
            dst.write(h)
            if h in (b'\r\n', b'\n'): break
            if h.lower().startswith(b'content-length'): n = int(h.split(b':')[1])
        body = src.read(n); dst.write(body); dst.flush()
        try:
            m = json.loads(body); what = m.get('method', '')
            if not what: what = 'answer ' + ('error %s' % m['error'].get('code') if 'error' in m else ('%d tokens' % (len(m['result']['data']) // 5) if isinstance(m.get('result'), dict) and 'data' in m['result'] else ''))
            if what == '$/cancelRequest': what += ' %s' % m['params']['id']
            if 'fileProgress' in what: what += ' %s' % ('done' if not m['params']['processing'] else 'to line %d' % m['params']['processing'][0]['range']['start']['line'])
            if any(k in what for k in ('semanticTokens', 'tokens', 'cancel', 'fileProgress', 'didOpen', 'didChange', 'error')) :
                with lock: log.write('%7.0f ms %s id=%s %s\n' % ((time.time() - t0) * 1000, tag, m.get('id', '-'), what)); log.flush()
        except Exception as e:
            with lock: log.write('? %r\n' % e)
threading.Thread(target=pump, args=(sys.stdin.buffer, p.stdin, '->'), daemon=True).start()
pump(p.stdout, sys.stdout.buffer, '<-')
