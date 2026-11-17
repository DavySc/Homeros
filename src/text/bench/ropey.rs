// ropey 1.6 on the workload of bench.c (char indices; the input is ASCII, so
// chars are bytes).  Build: see run.sh.
use std::time::Instant;
struct Rng(u64);
impl Rng { fn nx(&mut self) -> u64 { let mut x = self.0; x ^= x << 13; x ^= x >> 7; x ^= x << 17; self.0 = x; x } }
fn fnv(s: &[u8]) -> u64 { let mut h = 1469598103934665603u64; for &b in s { h ^= b as u64; h = h.wrapping_mul(1099511628211); } h }
enum Edit { Ins(usize, usize), Del(usize, String) }
fn main() {
    let a: Vec<String> = std::env::args().collect();
    let orig = std::fs::read_to_string(&a[1]).unwrap();
    let n: usize = a[2].parse().unwrap();
    let h0 = fnv(orig.as_bytes());
    let mut sum = 0usize;
    let mut tm = [Instant::now(); 5];
    let t = run(&orig, n, &mut sum, &mut tm);
    let out = t.to_string();
    println!("Rope load_s={:.3} edit_s={:.3} lines_s={:.3} undo_s={:.3} sum={} ok={}", (tm[1] - tm[0]).as_secs_f64(), (tm[2] - tm[1]).as_secs_f64(),
             (tm[3] - tm[2]).as_secs_f64(), (tm[4] - tm[3]).as_secs_f64(), sum, fnv(out.as_bytes()) == h0);
}

// The workload alone, so callgrind can count it by itself (--toggle-collect).
#[inline(never)]
fn run(orig: &str, n: usize, sum: &mut usize, tm: &mut [Instant; 5]) -> ropey::Rope {
    let mut r = Rng(88172645463325252);
    let mut log = Vec::with_capacity(n);
    tm[0] = Instant::now();
    let mut t = ropey::Rope::from_str(orig);
    tm[1] = Instant::now();
    for _ in 0..n {
        let len = t.len_chars();
        let pos = (r.nx() % (len as u64 + 1)) as usize;
        if r.nx() & 1 == 1 {
            let k = 1 + (r.nx() % 10) as usize;
            let s: String = (0..k).map(|_| (b'a' + (r.nx() % 26) as u8) as char).collect();
            t.insert(pos, &s); log.push(Edit::Ins(pos, k));
        } else {
            let k = (1 + (r.nx() % 10) as usize).min(len - pos);
            let d = t.slice(pos..pos + k).to_string(); t.remove(pos..pos + k); log.push(Edit::Del(pos, d));
        }
    }
    tm[2] = Instant::now();
    for _ in 0..n {
        // len_lines counts the line after the last '\n', so it is text_lines + 1
        *sum += t.line_to_char((r.nx() % t.len_lines() as u64) as usize);
        *sum += t.char_to_line((r.nx() % (t.len_chars() as u64 + 1)) as usize);
    }
    tm[3] = Instant::now();
    for e in log.iter().rev() { match e { Edit::Ins(p, k) => t.remove(*p..*p + *k), Edit::Del(p, d) => t.insert(*p, d) } }
    tm[4] = Instant::now();
    t
}
