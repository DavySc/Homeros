use unicode_segmentation::UnicodeSegmentation;
use unicode_width::UnicodeWidthStr;
fn main() {
    let a: Vec<String> = std::env::args().collect();
    let p: lsp_types::PublishDiagnosticsParams = serde_json::from_str(&a[1]).unwrap();
    let re = regex::Regex::new(&a[2]).unwrap();
    let mut parser = tree_sitter::Parser::new();
    let _ = parser.parse(&a[3], None);
    println!("{} {} {} {}", p.diagnostics.len(), re.is_match(&a[3]), a[3].graphemes(true).count(), a[3].width());
}
// x
// z
