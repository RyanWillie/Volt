//! S-Expression parser.
//!
//! Parses the LibrePCB S-Expression dialect into an [`SExpr`] tree.
//! Records the exact whitespace between elements for round-trip fidelity.

use crate::sexp::SExpr;
use thiserror::Error;

#[derive(Debug, Error)]
pub enum ParseError {
    #[error("unexpected end of input")]
    UnexpectedEof,
    #[error("unexpected character '{0}' at position {1}")]
    UnexpectedChar(char, usize),
    #[error("unterminated string starting at position {0}")]
    UnterminatedString(usize),
    #[error("trailing content after top-level expression at position {0}")]
    TrailingContent(usize),
}

/// Parse an S-Expression string into a tree.
///
/// Expects exactly one top-level expression (typically a list).
pub fn parse(input: &str) -> Result<SExpr, ParseError> {
    let mut pos = 0;
    skip_whitespace(input, &mut pos);
    if pos >= input.len() {
        return Err(ParseError::UnexpectedEof);
    }
    let expr = parse_expr(input, &mut pos)?;
    skip_whitespace(input, &mut pos);
    if pos < input.len() {
        return Err(ParseError::TrailingContent(pos));
    }
    Ok(expr)
}

fn parse_expr(input: &str, pos: &mut usize) -> Result<SExpr, ParseError> {
    // Note: caller is responsible for skipping leading whitespace
    if *pos >= input.len() {
        return Err(ParseError::UnexpectedEof);
    }

    let bytes = input.as_bytes();
    match bytes[*pos] {
        b'(' => parse_list(input, pos),
        b'"' => parse_string(input, pos),
        b')' => Err(ParseError::UnexpectedChar(')', *pos)),
        _ => parse_atom(input, pos),
    }
}

fn parse_list(input: &str, pos: &mut usize) -> Result<SExpr, ParseError> {
    debug_assert_eq!(input.as_bytes()[*pos], b'(');
    *pos += 1; // skip '('

    let mut children = Vec::new();
    let mut separators = Vec::new();

    loop {
        // Record whitespace before next element or closing paren
        let ws_start = *pos;
        skip_whitespace(input, pos);
        let ws = &input[ws_start..*pos];

        if *pos >= input.len() {
            return Err(ParseError::UnexpectedEof);
        }
        if input.as_bytes()[*pos] == b')' {
            *pos += 1; // skip ')'
            return Ok(SExpr::List {
                children,
                separators,
                closing: ws.to_string(),
            });
        }

        separators.push(ws.to_string());
        children.push(parse_expr(input, pos)?);
    }
}

fn parse_string(input: &str, pos: &mut usize) -> Result<SExpr, ParseError> {
    debug_assert_eq!(input.as_bytes()[*pos], b'"');
    let start = *pos;
    *pos += 1; // skip opening '"'

    let mut s = String::new();
    let bytes = input.as_bytes();
    while *pos < bytes.len() {
        match bytes[*pos] {
            b'"' => {
                *pos += 1; // skip closing '"'
                return Ok(SExpr::Str(s));
            }
            b'\\' => {
                *pos += 1;
                if *pos >= bytes.len() {
                    return Err(ParseError::UnterminatedString(start));
                }
                match bytes[*pos] {
                    b'"' => s.push('"'),
                    b'\\' => s.push('\\'),
                    b'n' => s.push('\n'),
                    b'r' => s.push('\r'),
                    b't' => s.push('\t'),
                    other => {
                        s.push('\\');
                        s.push(other as char);
                    }
                }
                *pos += 1;
            }
            _ => {
                let ch = input[*pos..].chars().next().unwrap();
                s.push(ch);
                *pos += ch.len_utf8();
            }
        }
    }
    Err(ParseError::UnterminatedString(start))
}

fn parse_atom(input: &str, pos: &mut usize) -> Result<SExpr, ParseError> {
    let start = *pos;
    let bytes = input.as_bytes();
    while *pos < bytes.len() {
        match bytes[*pos] {
            b' ' | b'\t' | b'\n' | b'\r' | b'(' | b')' | b'"' => break,
            _ => *pos += 1,
        }
    }
    if *pos == start {
        return Err(ParseError::UnexpectedEof);
    }
    Ok(SExpr::Atom(input[start..*pos].to_string()))
}

fn skip_whitespace(input: &str, pos: &mut usize) {
    let bytes = input.as_bytes();
    while *pos < bytes.len() {
        match bytes[*pos] {
            b' ' | b'\t' | b'\n' | b'\r' => *pos += 1,
            _ => break,
        }
    }
}
