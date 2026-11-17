// scen.c's scenarios on ropey 1.6.1 (ASCII input, so chars are bytes).
use std::time::Instant;
struct Rng(u64);
impl Rng { fn nx(&mut self) -> u64 { let mut x = self.0; x ^= x << 13; x ^= x >> 7; x ^= x << 17; self.0 = x; x } }
#[inline(never)]
fn run(t: &mut ropey::Rope, orig: &str, w: &str, sink: &mut usize) {
    let mut r = Rng(88172645463325252);
    let len = t.len_chars(); let mid = len / 2;
    match w {
        "open" => { *t = ropey::Rope::from_str(orig); }
        "type" => { for i in 0..100000 { t.insert(mid + i, if i % 60 == 59 { "\n" } else { "a" }); } }
        "backspace" => { for i in 0..100000 { let p = mid - 1 - i; t.remove(p..p + 1); } }
        "paste" => { for _ in 0..20 { let p = (r.nx() % (len as u64 - (1 << 20))) as usize; t.insert(p, &orig[p / 2..p / 2 + (1 << 20)]); t.remove(p..p + (1 << 20)); } }
        "bigdel" => { for _ in 0..5 { let p = (r.nx() % (len as u64 / 2)) as usize; t.remove(p..p + len / 2); t.insert(p, &orig[p..p + len / 2]); } }
        "screen" => { let l = t.len_lines() - 1; for _ in 0..100000 { let s = t.line_to_char((r.nx() % (l as u64 + 1)) as usize); let e = (s + 4000).min(len);
                       for c in t.slice(s..e).chunks() { *sink += c.len(); } } }
        "lines" => { for l in t.lines() { for c in l.chunks() { *sink += c.len(); } } }
        "readall" => { for c in t.chunks() { *sink += c.len() + c.as_bytes()[0] as usize; } }
        "randedit" => { for _ in 0..50000 { let l = t.len_chars(); let p = (r.nx() % (l as u64 + 1)) as usize; let k = 1 + (r.nx() % 10) as usize;
                        if r.nx() & 1 == 1 { t.insert(p, &"abcdefghij"[..k]); } else { t.remove(p..p + k.min(l - p)); } } }
        "lineof" => { for _ in 0..200000 { *sink += t.char_to_line((r.nx() % (len as u64 + 1)) as usize); } }
        "linestart" => { let l = t.len_lines() - 1; for _ in 0..200000 { *sink += t.line_to_char((r.nx() % (l as u64 + 1)) as usize); } }
        _ => panic!(),
    }
}
fn main() {
    let a: Vec<String> = std::env::args().collect();
    let orig = std::fs::read_to_string(&a[1]).unwrap();
    let mut t = if a[2] == "open" { ropey::Rope::new() } else { ropey::Rope::from_str(&orig) };
    let mut sink = 0usize;
    let t0 = Instant::now();
    run(&mut t, &orig, &a[2], &mut sink);
    println!("{} s={:.4} len={} sink={}", a[2], t0.elapsed().as_secs_f64(), t.len_chars(), sink % 1000);
}
