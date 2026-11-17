/// Same contract as ../u8/u8.c.
pub fn u8len(s: &[u8], i: usize) -> usize {
    let c = s[i];
    let k = if c < 0x80 { 1 } else if c >> 5 == 6 { 2 } else if c >> 4 == 14 { 3 } else if c >> 3 == 30 { 4 } else { 0 };
    if k == 0 || k > s.len() - i { return 1; }
    for j in 1..k { if s[i + j] & 0xC0 != 0x80 { return 1; } }
    k
}
#[cfg(kani)]
#[kani::proof]
#[kani::unwind(6)]
fn u8len_contract() {
    let buf: [u8; 8] = kani::any();
    let n: usize = kani::any(); kani::assume(n > 0 && n <= 8);
    let i: usize = kani::any(); kani::assume(i < n);
    let r = u8len(&buf[..n], i);
    assert!(1 <= r && r <= 4 && i + r <= n);
}
