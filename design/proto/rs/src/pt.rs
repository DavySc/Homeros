//! Piece table, same algorithm as ../c/pt.c.
#[derive(Clone, Copy)]
pub struct Piece { pub add: bool, pub start: usize, pub len: usize }
pub struct PT<'a> { pub orig: &'a [u8], pub add: Vec<u8>, pub p: Vec<Piece>, pub len: usize }

impl<'a> PT<'a> {
    pub fn new(orig: &'a [u8]) -> Self {
        let p = if orig.is_empty() { vec![] } else { vec![Piece { add: false, start: 0, len: orig.len() }] };
        PT { orig, add: Vec::new(), p, len: orig.len() }
    }
    fn src(&self, p: &Piece) -> &[u8] { if p.add { &self.add } else { self.orig } }
    fn split(&mut self, pos: usize) -> Option<usize> {
        let mut off = 0;
        let mut i = 0;
        while i < self.p.len() {
            if pos == off { return Some(i); }
            if pos < off + self.p[i].len { break; }
            off += self.p[i].len; i += 1;
        }
        if i == self.p.len() { return if pos == off { Some(i) } else { None }; }
        let k = pos - off;
        let mut right = self.p[i];
        right.start += k; right.len -= k;
        self.p[i].len = k;
        self.p.insert(i + 1, right);
        Some(i + 1)
    }
    pub fn insert(&mut self, pos: usize, s: &[u8]) -> Result<(), ()> {
        if pos > self.len { return Err(()); }
        if s.is_empty() { return Ok(()); }
        let i = self.split(pos).ok_or(())?;
        if i > 0 && self.p[i - 1].add && self.p[i - 1].start + self.p[i - 1].len == self.add.len() {
            self.p[i - 1].len += s.len();
        } else {
            self.p.insert(i, Piece { add: true, start: self.add.len(), len: s.len() });
        }
        self.add.extend_from_slice(s); self.len += s.len();
        Ok(())
    }
    pub fn delete(&mut self, pos: usize, n: usize, out: Option<&mut Vec<u8>>) -> Result<(), ()> {
        if pos > self.len || n > self.len - pos { return Err(()); }
        if n == 0 { return Ok(()); }
        if let Some(o) = out { self.read(pos, n, o); }
        let a = self.split(pos).ok_or(())?;
        let b = self.split(pos + n).ok_or(())?;
        self.p.drain(a..b);
        self.len -= n;
        Ok(())
    }
    pub fn read(&self, mut pos: usize, n: usize, out: &mut Vec<u8>) {
        let (mut off, mut w) = (0, 0);
        for p in &self.p {
            if w >= n { break; }
            if pos < off + p.len {
                let k = pos.saturating_sub(off);
                let m = (p.len - k).min(n - w);
                out.extend_from_slice(&self.src(p)[p.start + k..p.start + k + m]);
                w += m; pos += m;
            }
            off += p.len;
        }
    }
}

#[cfg(kani)]
mod verification {
    use super::*;
    /// Same property as ../c/cbmc_harness.c: an earlier insert+delete of "Z",
    /// then insert k bytes at pos and delete them again gives back the original.
    #[kani::proof]
    #[kani::unwind(8)]
    fn insert_delete_roundtrip() {
        let orig: [u8; 3] = kani::any();
        let s: [u8; 2] = kani::any();
        let p0: usize = kani::any(); kani::assume(p0 <= 3);
        let pos: usize = kani::any(); kani::assume(pos <= 3);
        let mut t = PT::new(&orig);
        t.add.reserve(8); t.p.reserve(16);
        t.insert(p0, b"Z").unwrap();
        t.delete(p0, 1, None).unwrap();
        t.insert(pos, &s).unwrap();
        assert!(t.len == 5);
        t.delete(pos, 2, None).unwrap();
        let mut out = Vec::with_capacity(3);
        t.read(0, t.len, &mut out);
        assert!(out.len() == 3 && out[..] == orig[..]);
    }
}
