//! Rigid transforms and uniform scaling.

use volt_cad_core::Operation;

use super::{begin_job, execute, finish_job, Globals};
use crate::parse::{expect_flag, optional_flag, parse_f64_flag, parse_vec3_csv};

pub fn run(globals: &Globals, tail: &[String]) -> Result<(), String> {
    if tail.is_empty() {
        return Err(
            "transform: missing subcommand (translate | rotate | scale)".to_string(),
        );
    }
    let mut ops = begin_job(globals);
    match tail[0].as_str() {
        "translate" => {
            let id = expect_flag(&tail[1..], "--id")?;
            let delta_s = expect_flag(&tail[1..], "--delta")?;
            let d = parse_vec3_csv(&delta_s, "--delta")?;
            ops.push(Operation::TransformTranslate {
                id,
                dx: d[0],
                dy: d[1],
                dz: d[2],
            });
        }
        "rotate" => {
            let id = expect_flag(&tail[1..], "--id")?;
            let origin = optional_flag(&tail[1..], "--origin")
                .map(|s| parse_vec3_csv(&s, "--origin"))
                .transpose()?
                .unwrap_or([0.0, 0.0, 0.0]);
            let axis_s = expect_flag(&tail[1..], "--axis")?;
            let axis = parse_vec3_csv(&axis_s, "--axis")?;
            let angle_deg = parse_f64_flag(&tail[1..], "--angle-deg", f64::NAN)?;
            if angle_deg.is_nan() {
                return Err("rotate: `--angle-deg` is required".to_string());
            }
            ops.push(Operation::TransformRotate {
                id,
                origin,
                axis,
                angle_deg,
            });
        }
        "scale" => {
            let id = expect_flag(&tail[1..], "--id")?;
            let uniform = parse_f64_flag(&tail[1..], "--uniform", f64::NAN)?;
            if uniform.is_nan() {
                return Err("scale: `--uniform` is required".to_string());
            }
            let center = optional_flag(&tail[1..], "--center")
                .map(|s| parse_vec3_csv(&s, "--center"))
                .transpose()?
                .unwrap_or([0.0, 0.0, 0.0]);
            ops.push(Operation::TransformScale {
                id,
                uniform,
                center,
            });
        }
        other => return Err(format!("transform: unknown subcommand `{other}`")),
    }
    ops = finish_job(globals, ops);
    execute(ops)
}
