//! Primitive solid creation.

use volt_cad_core::Operation;

use super::{execute, finish_job, Globals};
use crate::parse::{expect_flag, optional_flag, parse_f64_flag, parse_vec3_csv};

pub fn run(globals: &Globals, tail: &[String]) -> Result<(), String> {
    if tail.is_empty() {
        return Err("solid: missing subcommand (box | cylinder | sphere | cone)".to_string());
    }
    let mut ops = super::begin_job(globals);
    match tail[0].as_str() {
        "box" => {
            let id = expect_flag(&tail[1..], "--id")?;
            let origin = optional_flag(&tail[1..], "--origin")
                .map(|s| parse_vec3_csv(&s, "--origin"))
                .transpose()?
                .unwrap_or([0.0, 0.0, 0.0]);
            let size_s = expect_flag(&tail[1..], "--size")?;
            let size = parse_vec3_csv(&size_s, "--size")?;
            ops.push(Operation::SolidBox { id, origin, size });
        }
        "cylinder" => {
            let id = expect_flag(&tail[1..], "--id")?;
            let base = optional_flag(&tail[1..], "--base")
                .map(|s| parse_vec3_csv(&s, "--base"))
                .transpose()?
                .unwrap_or([0.0, 0.0, 0.0]);
            let axis = optional_flag(&tail[1..], "--axis")
                .map(|s| parse_vec3_csv(&s, "--axis"))
                .transpose()?
                .unwrap_or([0.0, 0.0, 1.0]);
            let radius = parse_f64_flag(&tail[1..], "--radius", f64::NAN)?;
            if radius.is_nan() {
                return Err("cylinder: `--radius` is required".to_string());
            }
            let height = parse_f64_flag(&tail[1..], "--height", f64::NAN)?;
            if height.is_nan() {
                return Err("cylinder: `--height` is required".to_string());
            }
            ops.push(Operation::SolidCylinder {
                id,
                base,
                axis,
                radius,
                height,
            });
        }
        "sphere" => {
            let id = expect_flag(&tail[1..], "--id")?;
            let center = optional_flag(&tail[1..], "--center")
                .map(|s| parse_vec3_csv(&s, "--center"))
                .transpose()?
                .unwrap_or([0.0, 0.0, 0.0]);
            let radius = parse_f64_flag(&tail[1..], "--radius", f64::NAN)?;
            if radius.is_nan() {
                return Err("sphere: `--radius` is required".to_string());
            }
            ops.push(Operation::SolidSphere {
                id,
                center,
                radius,
            });
        }
        "cone" => {
            let id = expect_flag(&tail[1..], "--id")?;
            let base = optional_flag(&tail[1..], "--base")
                .map(|s| parse_vec3_csv(&s, "--base"))
                .transpose()?
                .unwrap_or([0.0, 0.0, 0.0]);
            let axis = optional_flag(&tail[1..], "--axis")
                .map(|s| parse_vec3_csv(&s, "--axis"))
                .transpose()?
                .unwrap_or([0.0, 0.0, 1.0]);
            let r1 = parse_f64_flag(&tail[1..], "--r1", f64::NAN)?;
            let r2 = parse_f64_flag(&tail[1..], "--r2", f64::NAN)?;
            let height = parse_f64_flag(&tail[1..], "--height", f64::NAN)?;
            if r1.is_nan() || r2.is_nan() || height.is_nan() {
                return Err("cone: `--r1`, `--r2`, and `--height` are required".to_string());
            }
            ops.push(Operation::SolidCone {
                id,
                base,
                axis,
                r1,
                r2,
                height,
            });
        }
        other => return Err(format!("solid: unknown subcommand `{other}`")),
    }
    ops = finish_job(globals, ops);
    execute(ops)
}
