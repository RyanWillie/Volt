//! S-Expression writer.
//!
//! Replays the stored whitespace separators to reproduce the original formatting.
//! For programmatically-constructed nodes, uses default formatting (space separators).

use crate::sexp::SExpr;
use std::fmt;

pub fn write_sexpr(expr: &SExpr, f: &mut fmt::Formatter<'_>) -> fmt::Result {
    match expr {
        SExpr::Atom(s) => write!(f, "{s}"),
        SExpr::Str(s) => write!(f, "\"{}\"", escape_string(s)),
        SExpr::List {
            children,
            separators,
            closing,
        } => {
            write!(f, "(")?;
            for (i, child) in children.iter().enumerate() {
                let sep = separators.get(i).map(String::as_str).unwrap_or(" ");
                write!(f, "{sep}")?;
                write_sexpr(child, f)?;
            }
            write!(f, "{closing})")
        }
    }
}

fn escape_string(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    for ch in s.chars() {
        match ch {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            _ => out.push(ch),
        }
    }
    out
}
