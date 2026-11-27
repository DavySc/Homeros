import sys, re, json, yaml
t = open(sys.argv[1]).read()
y = yaml.safe_load(t)
expr = y['jobs']['check']['strategy']['matrix']['target']
m = re.fullmatch(r"\$\{\{ fromJSON\(github\.head_ref == 'claude/project-thread-clivv8'\s*&& '([^']*)'\s*\|\| '([^']*)'\) \}\}", expr, re.S)
train, other = json.loads(m.group(1)), json.loads(m.group(2))
sh = [x for x in train if x.startswith('planted')]
ks = sorted(int(re.fullmatch(r'planted SHARD=(\d+)/(\d+)', x).group(1)) for x in sh)
ns = {int(x.split('/')[1]) for x in sh}
print('train jobs', len(train), 'check' in train, 'shards', len(sh), 'n', ns, 'k is 0..n-1:', ks == list(range(len(sh))) and ns == {len(sh)}, '| other', other,
      '| fail-fast', y['jobs']['check']['strategy']['fail-fast'], 'timeout', y['jobs']['check']['timeout-minutes'])
