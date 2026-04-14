//! Minimal argv parsing (compatible with older Cargo toolchains).

use std::path::PathBuf;

#[derive(Debug, Clone)]
pub struct Globals {
    pub document: Option<PathBuf>,
    pub new_doc_name: String,
    pub save_document: Option<PathBuf>,
}

impl Default for Globals {
    fn default() -> Self {
        Self {
            document: None,
            new_doc_name: "VoltCAD".to_string(),
            save_document: None,
        }
    }
}

pub fn parse_globals(args: &[String]) -> Result<(Globals, Vec<String>), String> {
    let mut g = Globals::default();
    let mut i = 0usize;
    while i < args.len() {
        let a = &args[i];
        if a == "--document" {
            let v = args
                .get(i + 1)
                .ok_or_else(|| "--document requires a path".to_string())?;
            g.document = Some(PathBuf::from(v));
            i += 2;
        } else if a == "--new-doc-name" {
            let v = args
                .get(i + 1)
                .ok_or_else(|| "--new-doc-name requires a value".to_string())?;
            g.new_doc_name = v.clone();
            i += 2;
        } else if a == "--save-document" {
            let v = args
                .get(i + 1)
                .ok_or_else(|| "--save-document requires a path".to_string())?;
            g.save_document = Some(PathBuf::from(v));
            i += 2;
        } else if a.starts_with('-') {
            return Err(format!("unknown global flag `{a}`"));
        } else {
            break;
        }
    }
    Ok((g, args[i..].to_vec()))
}

pub fn expect_flag(args: &[String], name: &str) -> Result<String, String> {
    for (idx, a) in args.iter().enumerate() {
        if a == name {
            return args
                .get(idx + 1)
                .cloned()
                .ok_or_else(|| format!("{name} requires a value"));
        }
    }
    Err(format!("missing required flag `{name}`"))
}

pub fn optional_flag(args: &[String], name: &str) -> Option<String> {
    for (idx, a) in args.iter().enumerate() {
        if a == name {
            return args.get(idx + 1).cloned();
        }
    }
    None
}

pub fn parse_vec3_csv(s: &str, label: &str) -> Result<[f64; 3], String> {
    let parts: Vec<&str> = s.split(',').map(|p| p.trim()).collect();
    if parts.len() != 3 {
        return Err(format!("{label} must be three comma-separated numbers"));
    }
    let mut out = [0.0_f64; 3];
    for (i, p) in parts.into_iter().enumerate() {
        out[i] = p
            .parse::<f64>()
            .map_err(|_| format!("invalid number in {label}: `{p}`"))?;
    }
    Ok(out)
}

pub fn parse_f64_flag(args: &[String], name: &str, default: f64) -> Result<f64, String> {
    if let Some(v) = optional_flag(args, name) {
        v.parse::<f64>()
            .map_err(|_| format!("invalid number for `{name}`: `{v}`"))
    } else {
        Ok(default)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_globals_splits_rest() {
        let args = vec![
            "--new-doc-name".to_string(),
            "DocA".to_string(),
            "solid".to_string(),
            "box".to_string(),
        ];
        let (g, rest) = parse_globals(&args).unwrap();
        assert_eq!(g.new_doc_name, "DocA");
        assert_eq!(rest, vec!["solid".to_string(), "box".to_string()]);
    }

    #[test]
    fn parse_vec3_accepts_spaces() {
        let v = parse_vec3_csv(" 1 , 2 , 3 ", "x").unwrap();
        assert_eq!(v, [1.0, 2.0, 3.0]);
    }
}
