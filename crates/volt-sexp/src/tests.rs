use crate::{SExpr, parse};
use std::path::Path;

#[test]
fn parse_atom() {
    let expr = parse("hello").unwrap();
    assert_eq!(expr, SExpr::Atom("hello".into()));
}

#[test]
fn parse_string() {
    let expr = parse(r#""hello world""#).unwrap();
    assert_eq!(expr, SExpr::Str("hello world".into()));
}

#[test]
fn parse_string_with_escapes() {
    let expr = parse(r#""line1\nline2""#).unwrap();
    assert_eq!(expr, SExpr::Str("line1\nline2".into()));

    let expr = parse(r#""a\"b""#).unwrap();
    assert_eq!(expr, SExpr::Str("a\"b".into()));
}

#[test]
fn parse_empty_list() {
    let expr = parse("()").unwrap();
    assert!(expr.is_inline());
    assert!(expr.children().is_empty());
}

#[test]
fn parse_flat_list() {
    let expr = parse(r#"(name "test")"#).unwrap();
    assert_eq!(expr.keyword(), Some("name"));
    assert_eq!(expr.args()[0].as_str(), Some("test"));
    assert!(expr.is_inline());
}

#[test]
fn parse_nested_inline() {
    let expr = parse("(grid (interval 1.0) (unit millimeters))").unwrap();
    assert!(expr.is_inline());
    assert_eq!(expr.keyword(), Some("grid"));
}

#[test]
fn parse_nested_block() {
    let input = "(root\n (name \"test\")\n)";
    let expr = parse(input).unwrap();
    assert!(!expr.is_inline());
    assert_eq!(expr.keyword(), Some("root"));
}

#[test]
fn parse_uuid_and_numbers() {
    let expr = parse("(test 34ce99d2-d946-43c5-a01b-80a9f9463716 3.14 -1.5)").unwrap();
    let args = expr.args();
    assert_eq!(args[0].as_atom(), Some("34ce99d2-d946-43c5-a01b-80a9f9463716"));
    assert_eq!(args[1].as_atom(), Some("3.14"));
    assert_eq!(args[2].as_atom(), Some("-1.5"));
}

#[test]
fn keyword_and_child_access() {
    let expr = parse("(root\n (name \"foo\")\n (version \"1.0\")\n)").unwrap();
    assert_eq!(expr.keyword(), Some("root"));
    assert_eq!(
        expr.child("name").unwrap().args()[0].as_str(),
        Some("foo")
    );
    assert_eq!(
        expr.child("version").unwrap().args()[0].as_str(),
        Some("1.0")
    );
    assert!(expr.child("missing").is_none());
}

#[test]
fn write_inline_list() {
    let expr = SExpr::list(vec![
        SExpr::Atom("name".into()),
        SExpr::Str("test".into()),
    ]);
    assert_eq!(expr.to_string(), r#"(name "test")"#);
}

#[test]
fn write_block_list() {
    let expr = SExpr::block_list_at(
        vec![
            SExpr::Atom("root".into()),
            SExpr::list(vec![
                SExpr::Atom("name".into()),
                SExpr::Str("test".into()),
            ]),
        ],
        0,
    );
    let expected = "(root\n (name \"test\")\n)";
    assert_eq!(expr.to_string(), expected);
}

#[test]
fn roundtrip_simple() {
    let inputs = &[
        r#"(name "test")"#,
        "(grid (interval 1.0) (unit millimeters))",
        "(root\n (name \"test\")\n)",
        "(a 1 2\n (b 3)\n (c 4)\n)",
    ];
    for input in inputs {
        let expr = parse(input).unwrap();
        assert_eq!(&expr.to_string(), input, "roundtrip failed for: {input}");
    }
}

#[test]
fn parse_error_unterminated_string() {
    assert!(parse(r#""hello"#).is_err());
}

#[test]
fn parse_error_trailing_content() {
    assert!(parse("(a) (b)").is_err());
}

#[test]
fn parse_error_unexpected_close() {
    assert!(parse(")").is_err());
}

// ---- Round-trip tests against real LibrePCB files ----

fn roundtrip_file(path: &Path) {
    let input = std::fs::read_to_string(path)
        .unwrap_or_else(|e| panic!("Failed to read {}: {e}", path.display()));

    // Normalize: trim trailing whitespace/newlines (files may have trailing newline)
    let input = input.trim_end();

    let expr = parse(input)
        .unwrap_or_else(|e| panic!("Failed to parse {}: {e}", path.display()));

    let output = expr.to_string();

    if input != output {
        // Find first difference for debugging
        let input_lines: Vec<&str> = input.lines().collect();
        let output_lines: Vec<&str> = output.lines().collect();
        for (i, (a, b)) in input_lines.iter().zip(output_lines.iter()).enumerate() {
            if a != b {
                panic!(
                    "Round-trip mismatch in {} at line {}:\n  expected: {:?}\n  got:      {:?}",
                    path.display(),
                    i + 1,
                    a,
                    b,
                );
            }
        }
        if input_lines.len() != output_lines.len() {
            panic!(
                "Round-trip line count mismatch in {}: expected {} lines, got {}",
                path.display(),
                input_lines.len(),
                output_lines.len(),
            );
        }
        panic!(
            "Round-trip mismatch in {} (no line diff found, possible whitespace issue)",
            path.display()
        );
    }
}

fn all_lp_files() -> Vec<std::path::PathBuf> {
    let test_dir = Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .unwrap()
        .parent()
        .unwrap()
        .join("tests/data/projects");

    let mut files = Vec::new();
    fn walk(dir: &Path, files: &mut Vec<std::path::PathBuf>) {
        if let Ok(entries) = std::fs::read_dir(dir) {
            for entry in entries.flatten() {
                let path = entry.path();
                if path.is_dir() {
                    walk(&path, files);
                } else if path.extension().is_some_and(|e| e == "lp") {
                    files.push(path);
                }
            }
        }
    }
    walk(&test_dir, &mut files);
    files.sort();
    files
}

#[test]
fn roundtrip_all_lp_files() {
    let files = all_lp_files();
    assert!(!files.is_empty(), "No .lp test files found!");
    eprintln!("Testing round-trip on {} .lp files", files.len());
    for file in &files {
        roundtrip_file(file);
    }
}
