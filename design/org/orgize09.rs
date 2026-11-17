// Summarise org files with orgize 0.9, same counts as el-sum.el.
use orgize::{elements::Element, Event, Org, ParseConfig};
use serde_json::{json, Map, Value};
fn main() {
    let cfg = ParseConfig { todo_keywords: (vec!["TODO".into(), "NEXT".into(), "WAIT".into()], vec!["DONE".into(), "CANCELLED".into()]), ..Default::default() };
    let mut out = Map::new();
    for f in std::env::args().skip(1) {
        let s = std::fs::read_to_string(&f).unwrap_or_default();
        let org = Org::parse_custom(&s, &cfg);
        let (mut h, mut h1, mut h3, mut todo, mut tagged, mut src, mut ex, mut tab, mut tsn, mut pd, mut pl) = (0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        for e in org.iter() {
            if let Event::Start(el) = e {
                match el {
                    Element::Title(t) => { h += 1; if t.level == 1 { h1 += 1 } if t.level >= 3 { h3 += 1 } if t.keyword.is_some() { todo += 1 } if !t.tags.is_empty() { tagged += 1 } if t.properties.iter().next().is_some() { pd += 1 } }
                    Element::SourceBlock(_) => src += 1,
                    Element::ExampleBlock(_) => ex += 1,
                    Element::Table(_) => tab += 1,
                    Element::Timestamp(_) => tsn += 1,
                    Element::List(_) => pl += 1,
                    _ => {}
                }
            }
        }
        out.insert(f.rsplit('/').next().unwrap().into(), json!({"headlines":h,"h1":h1,"h3plus":h3,"todo":todo,"tagged":tagged,"src_blocks":src,"example_blocks":ex,"tables":tab,"timestamps":tsn,"property_drawers":pd,"plain_lists":pl}));
    }
    println!("{}", Value::Object(out));
}
