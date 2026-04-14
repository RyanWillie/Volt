//! Generic S-Expression parser and writer for LibrePCB `.lp` files.
//!
//! The format is a simple S-Expression dialect:
//! - Lists: `(keyword child1 child2 ...)`
//! - Atoms: bare words, UUIDs, numbers, timestamps
//! - Strings: double-quoted with `\"`, `\\`, `\n` escapes
//!
//! # Round-trip fidelity
//!
//! The parser records the exact whitespace separator before each child element.
//! The writer replays these separators to reproduce the original formatting exactly.
//!
//! # Example
//! ```ignore
//! use volt_import::sexp::{SExpr, parse};
//!
//! let input = r#"(metadata 34ce99d2-d946-43c5-a01b-80a9f9463716
//!  (name "test")
//! )"#;
//! let expr = parse(input).unwrap();
//! let output = expr.to_string();
//! assert_eq!(input, output);
//! ```

mod parser;
mod writer;

use std::fmt;
pub use parser::{parse, ParseError};

/// A node in an S-Expression tree.
#[derive(Debug, Clone, PartialEq)]
pub enum SExpr {
    /// A bare token: identifier, UUID, number, boolean, timestamp, `none`, `auto`, etc.
    Atom(String),
    /// A double-quoted string.
    Str(String),
    /// A parenthesised list: `(child0 child1 child2 ...)`.
    ///
    /// `separators[i]` is the whitespace string before `children[i]`.
    /// `closing` is the whitespace before the closing `)`.
    ///
    /// For freshly constructed nodes (not parsed), separators/closing can be
    /// empty and the writer will use default formatting.
    List {
        children: Vec<SExpr>,
        /// Whitespace before each child. `separators.len() == children.len()`.
        separators: Vec<String>,
        /// Whitespace before the closing `)`.
        closing: String,
    },
}

impl SExpr {
    /// Create a new inline list (single space separators, no newlines).
    pub fn list(children: Vec<SExpr>) -> Self {
        let mut separators = Vec::with_capacity(children.len());
        for i in 0..children.len() {
            separators.push(if i == 0 { String::new() } else { " ".to_string() });
        }
        SExpr::List {
            children,
            separators,
            closing: String::new(),
        }
    }

    /// Create a block list with default indentation.
    /// Leading atoms go on the first line, then each remaining child on its own line.
    pub fn block_list_at(children: Vec<SExpr>, indent: usize) -> Self {
        let child_indent = indent + 1;
        let mut separators = Vec::with_capacity(children.len());
        let mut past_head = false;
        for (i, child) in children.iter().enumerate() {
            if i == 0 {
                separators.push(String::new());
            } else if !past_head && !matches!(child, SExpr::List { .. }) {
                separators.push(" ".to_string());
            } else {
                past_head = true;
                let mut sep = String::from("\n");
                for _ in 0..child_indent {
                    sep.push(' ');
                }
                separators.push(sep);
            }
        }
        let mut closing = String::from("\n");
        for _ in 0..indent {
            closing.push(' ');
        }
        SExpr::List {
            children,
            separators,
            closing,
        }
    }

    /// Returns the keyword (first atom) of a list node, if applicable.
    pub fn keyword(&self) -> Option<&str> {
        match self {
            SExpr::List { children, .. } => match children.first()? {
                SExpr::Atom(s) => Some(s),
                _ => None,
            },
            _ => None,
        }
    }

    /// Returns the children after the keyword for a list node.
    pub fn args(&self) -> &[SExpr] {
        match self {
            SExpr::List { children, .. } if !children.is_empty() => &children[1..],
            _ => &[],
        }
    }

    /// Returns all children of a list node.
    pub fn children(&self) -> &[SExpr] {
        match self {
            SExpr::List { children, .. } => children,
            _ => &[],
        }
    }

    /// Returns a mutable reference to children of a list node.
    pub fn children_mut(&mut self) -> Option<&mut Vec<SExpr>> {
        match self {
            SExpr::List { children, .. } => Some(children),
            _ => None,
        }
    }

    /// Finds the first child list with the given keyword.
    pub fn child(&self, keyword: &str) -> Option<&SExpr> {
        self.children()
            .iter()
            .find(|c| c.keyword() == Some(keyword))
    }

    /// Finds all child lists with the given keyword.
    pub fn children_with(&self, keyword: &str) -> Vec<&SExpr> {
        self.children()
            .iter()
            .filter(|c| c.keyword() == Some(keyword))
            .collect()
    }

    /// Returns the atom text if this is an Atom.
    pub fn as_atom(&self) -> Option<&str> {
        match self {
            SExpr::Atom(s) => Some(s),
            _ => None,
        }
    }

    /// Returns the string contents if this is a Str.
    pub fn as_str(&self) -> Option<&str> {
        match self {
            SExpr::Str(s) => Some(s),
            _ => None,
        }
    }

    /// Returns whether this list is inline (no newlines in separators or closing).
    pub fn is_inline(&self) -> bool {
        match self {
            SExpr::List {
                separators,
                closing,
                ..
            } => {
                !separators.iter().any(|s| s.contains('\n'))
                    && !closing.contains('\n')
            }
            _ => true,
        }
    }
}

impl fmt::Display for SExpr {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        writer::write_sexpr(self, f)
    }
}

// Tests are in the original sexp crate under volt-import/sexp/
