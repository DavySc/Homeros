// bench.c's steps on ropey 1.6.1.  The input is ASCII, so ropey's char
// indices are byte offsets and both do the same edits.
use std::time::Instant;
struct Rng(u64);
impl Rng { fn nx(&mut self) -> u64 { let mut x = self.0; x ^= x << 13; x ^= x >> 7; x ^= x << 17; self.0 = x; x } }
enum Edit { Ins(usize, usize), Del(usize, String) }
fn main() {
    let a: Vec<String> = std::env::args().collect();
    let orig = std::fs::read_to_string(&a[1]).unwrap();
    let (n, q): (usize, usize) = (a[2].parse().unwrap(), a[3].parse().unwrap());
    let mut r = Rng(88172645463325252);
    let mut log = Vec::with_capacity(n);
    let mut sink = 0usize;
    let t0 = Instant::now();
    let mut t = ropey::Rope::from_str(&orig);
    let t1 = Instant::now();
    for _ in 0..n {
        let len = t.len_chars();
        let pos = (r.nx() % (len as u64 + 1)) as usize;
        let k = 1 + (r.nx() % 10) as usize;
        if r.nx() & 1 == 1 {
            let s: String = (0..k).map(|_| (b'a' + (r.nx() % 26) as u8) as char).collect();
            t.insert(pos, &s); log.push(Edit::Ins(pos, k));
        } else {
            let k = k.min(len - pos);
            let d = t.slice(pos..pos + k).to_string(); t.remove(pos..pos + k); log.push(Edit::Del(pos, d));
        }
    }
    let t2 = Instant::now();
    for e in log.iter().rev() { match e { Edit::Ins(p, k) => t.remove(*p..*p + *k), Edit::Del(p, d) => t.insert(*p, d) } }
    let t3 = Instant::now();
    let ok = t.to_string() == orig;
    let t4 = Instant::now();
    let (lines, len) = (t.len_lines() - 1, t.len_chars());
    for _ in 0..q {
        sink += t.line_to_char((r.nx() % (lines as u64 + 1)) as usize);
        sink += t.char_to_line((r.nx() % (len as u64 + 1)) as usize);
    }
    let t5 = Instant::now();
    let s = |a: Instant, b: Instant| (b - a).as_secs_f64();
    println!("Rope open_s={:.3} edit_s={:.3} undo_s={:.3} read_s={:.3} lookup_s={:.3} ok={} sink={}",
             s(t0, t1), s(t1, t2), s(t2, t3), s(t3, t4), s(t4, t5), ok as i32, sink % 10);
}
