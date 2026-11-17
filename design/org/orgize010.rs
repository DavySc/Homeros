// Same counts with orgize 0.10 (rowan syntax tree).
use orgize::rowan::ast::AstNode;
use orgize::{Org, ParseConfig, SyntaxKind as K};
use serde_json::{json, Map, Value};
fn main() {
    let cfg = ParseConfig { todo_keywords: (vec!["TODO".into(), "NEXT".into(), "WAIT".into()], vec!["DONE".into(), "CANCELLED".into()]), ..Default::default() };
    let mut out = Map::new();
    for f in std::env::args().skip(1) {
        let s = std::fs::read_to_string(&f).unwrap_or_default();
        let org = cfg.clone().parse(&s);
        let mut c = std::collections::HashMap::<&str, usize>::new();
        for n in org.document().syntax().descendants() {
            let k = match n.kind() { K::HEADLINE => "headlines", K::SOURCE_BLOCK => "src_blocks", K::EXAMPLE_BLOCK => "example_blocks", K::ORG_TABLE => "tables", K::TIMESTAMP_ACTIVE | K::TIMESTAMP_INACTIVE | K::TIMESTAMP_DIARY => "timestamps", K::PROPERTY_DRAWER => "property_drawers", K::LIST => "plain_lists", _ => continue };
            *c.entry(k).or_default() += 1;
        }
        out.insert(f.rsplit('/').next().unwrap().into(), json!(c));
    }
    let _ = Org::parse("");
    println!("{}", Value::Object(out));
}
