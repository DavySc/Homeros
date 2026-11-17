mod pt;
use std::time::Instant;
struct Rng(u64);
impl Rng { fn nx(&mut self) -> u64 { let mut x = self.0; x ^= x << 13; x ^= x >> 7; x ^= x << 17; self.0 = x; x } }
fn fnv(s: &[u8]) -> u64 { let mut h = 1469598103934665603u64; for &b in s { h ^= b as u64; h = h.wrapping_mul(1099511628211); } h }
enum Edit { Ins(usize, usize), Del(usize, Vec<u8>) }
fn main() {
    let a: Vec<String> = std::env::args().collect();
    let orig = std::fs::read(&a[1]).unwrap();
    let n: usize = a[2].parse().unwrap();
    let rope = a.get(3).map(|s| s == "rope").unwrap_or(false);
    let h0 = fnv(&orig);
    let mut r = Rng(88172645463325252);
    let mut log = Vec::with_capacity(n);
    if !rope {
        let t0 = Instant::now();
        let mut t = pt::PT::new(&orig);
        for _ in 0..n {
            let pos = (r.nx() % (t.len as u64 + 1)) as usize;
            if r.nx() & 1 == 1 {
                let k = 1 + (r.nx() % 10) as usize;
                let s: Vec<u8> = (0..k).map(|_| b'a' + (r.nx() % 26) as u8).collect();
                t.insert(pos, &s).unwrap(); log.push(Edit::Ins(pos, k));
            } else {
                let k = (1 + (r.nx() % 10) as usize).min(t.len - pos);
                let mut d = Vec::new(); t.delete(pos, k, Some(&mut d)).unwrap(); log.push(Edit::Del(pos, d));
            }
        }
        let t1 = Instant::now();
        for e in log.iter().rev() { match e { Edit::Ins(p, k) => t.delete(*p, *k, None).unwrap(), Edit::Del(p, d) => t.insert(*p, d).unwrap() } }
        let t2 = Instant::now();
        let mut out = Vec::new(); t.read(0, t.len, &mut out);
        println!("Rs  edits={} edit_s={:.3} undo_s={:.3} pieces_after_undo={} ok={}", n, (t1 - t0).as_secs_f64(), (t2 - t1).as_secs_f64(), t.p.len(), fnv(&out) == h0);
    } else {
        // Same workload on ropey (char indices; the file is ASCII so chars == bytes).
        let t0 = Instant::now();
        let s = std::str::from_utf8(&orig).unwrap();
        let mut t = ropey::Rope::from_str(s);
        for _ in 0..n {
            let len = t.len_chars();
            let pos = (r.nx() % (len as u64 + 1)) as usize;
            if r.nx() & 1 == 1 {
                let k = 1 + (r.nx() % 10) as usize;
                let s: String = (0..k).map(|_| (b'a' + (r.nx() % 26) as u8) as char).collect();
                t.insert(pos, &s); log.push(Edit::Ins(pos, k));
            } else {
                let k = (1 + (r.nx() % 10) as usize).min(len - pos);
                let d = t.slice(pos..pos + k).to_string().into_bytes(); t.remove(pos..pos + k); log.push(Edit::Del(pos, d));
            }
        }
        let t1 = Instant::now();
        for e in log.iter().rev() { match e { Edit::Ins(p, k) => t.remove(*p..*p + *k), Edit::Del(p, d) => t.insert(*p, std::str::from_utf8(d).unwrap()) } }
        let t2 = Instant::now();
        let out = t.to_string();
        println!("Rope edits={} edit_s={:.3} undo_s={:.3} ok={}", n, (t1 - t0).as_secs_f64(), (t2 - t1).as_secs_f64(), fnv(out.as_bytes()) == h0);
    }
}
