# Generates the benchmark inputs: big.txt (100 MB) and mid.txt (first 10 MB).
import random; random.seed(1)
w = ['theorem','lemma','by','simp','intro','exact','fun','x','y','Nat','List','have','calc','rfl']
with open('big.txt', 'w') as f:
    n = 0
    while n < 100_000_000:
        l = ' '.join(random.choice(w) for _ in range(random.randint(3, 14))) + '\n'; f.write(l); n += len(l)
open('mid.txt', 'wb').write(open('big.txt', 'rb').read(10_000_000))
