//! Minimal STEP (AP214) export for board outline.
//!
//! Generates a STEP file containing the board as an extruded polygon.
//! Component 3D models are not included in this initial version.

use std::fmt::Write;

use volt_core::common::*;
use volt_core::project::*;

/// Export a minimal STEP file with the board outline extruded to board thickness.
pub fn export_step(board: &Board) -> String {
    let outline = board
        .polygons
        .iter()
        .find(|p| p.layer == Layer::BoardOutlines);

    let vertices: Vec<(f64, f64)> = outline
        .map(|p| {
            p.vertices
                .iter()
                .map(|v| (v.position.x, v.position.y))
                .collect()
        })
        .unwrap_or_default();

    if vertices.len() < 3 {
        return "ISO-10303-21;\nHEADER;\nENDSEC;\nDATA;\nENDSEC;\nEND-ISO-10303-21;\n".into();
    }

    let thickness = board.thickness;
    let mut out = String::with_capacity(8192);
    let mut id = 1usize;

    // STEP header
    writeln!(out, "ISO-10303-21;").unwrap();
    writeln!(out, "HEADER;").unwrap();
    writeln!(out, "FILE_DESCRIPTION(('Volt EDA Board Export'),'2;1');").unwrap();
    writeln!(
        out,
        "FILE_NAME('{}','2026-04-17',('Volt EDA'),(''),'',' ','');",
        board.name
    )
    .unwrap();
    writeln!(out, "FILE_SCHEMA(('AUTOMOTIVE_DESIGN'));").unwrap();
    writeln!(out, "ENDSEC;").unwrap();
    writeln!(out, "DATA;").unwrap();

    // Application context
    let app_ctx = id;
    id += 1;
    writeln!(
        out,
        "#{}=APPLICATION_CONTEXT('automotive design');",
        app_ctx
    )
    .unwrap();

    let app_proto = id;
    id += 1;
    writeln!(
        out,
        "#{}=APPLICATION_PROTOCOL_DEFINITION('','automotive_design',2010,#{});",
        app_proto, app_ctx
    )
    .unwrap();

    // Product
    let product = id;
    id += 1;
    writeln!(
        out,
        "#{}=PRODUCT('{}','{}','',(#{}));",
        product, board.name, board.name, id
    )
    .unwrap();

    let prod_ctx = id;
    id += 1;
    writeln!(
        out,
        "#{}=PRODUCT_CONTEXT('',#{}, 'mechanical');",
        prod_ctx, app_ctx
    )
    .unwrap();

    let prod_def_form = id;
    id += 1;
    writeln!(
        out,
        "#{}=PRODUCT_DEFINITION_FORMATION('','',#{});",
        prod_def_form, product
    )
    .unwrap();

    let prod_def_ctx = id;
    id += 1;
    writeln!(
        out,
        "#{}=PRODUCT_DEFINITION_CONTEXT('design',#{}, 'design');",
        prod_def_ctx, app_ctx
    )
    .unwrap();

    let prod_def = id;
    id += 1;
    writeln!(
        out,
        "#{}=PRODUCT_DEFINITION('design','',#{},#{});",
        prod_def, prod_def_form, prod_def_ctx
    )
    .unwrap();

    // Shape
    let shape_def = id;
    id += 1;
    let shape_rep = id;
    id += 1;
    writeln!(
        out,
        "#{}=PRODUCT_DEFINITION_SHAPE('','',#{});",
        shape_def, prod_def
    )
    .unwrap();

    let shape_def_rep = id;
    id += 1;
    writeln!(
        out,
        "#{}=SHAPE_DEFINITION_REPRESENTATION(#{},#{});",
        shape_def_rep, shape_def, shape_rep
    )
    .unwrap();

    // Axis placement for the shape
    let origin = id;
    id += 1;
    writeln!(out, "#{}=CARTESIAN_POINT('Origin',(0.0,0.0,0.0));", origin).unwrap();

    let dir_z = id;
    id += 1;
    writeln!(out, "#{}=DIRECTION('Z',(0.0,0.0,1.0));", dir_z).unwrap();

    let dir_x = id;
    id += 1;
    writeln!(out, "#{}=DIRECTION('X',(1.0,0.0,0.0));", dir_x).unwrap();

    let axis = id;
    id += 1;
    writeln!(
        out,
        "#{}=AXIS2_PLACEMENT_3D('',#{},#{},#{});",
        axis, origin, dir_z, dir_x
    )
    .unwrap();

    // Board outline as a polyline face
    // Create cartesian points for bottom face
    let mut bottom_pts = Vec::new();
    for (x, y) in &vertices {
        let pt = id;
        id += 1;
        writeln!(out, "#{}=CARTESIAN_POINT('',({:.6},{:.6},0.0));", pt, x, y).unwrap();
        bottom_pts.push(pt);
    }

    // Create cartesian points for top face
    let mut top_pts = Vec::new();
    for (x, y) in &vertices {
        let pt = id;
        id += 1;
        writeln!(
            out,
            "#{}=CARTESIAN_POINT('',({:.6},{:.6},{:.6}));",
            pt, x, y, thickness
        )
        .unwrap();
        top_pts.push(pt);
    }

    // Create a closed shell representation (simplified — just enumerate vertices)
    // For a proper STEP file we'd need B-rep topology; for now emit a basic
    // geometric representation with the outline polyline and extrusion info

    // Polyline for bottom outline
    let polyline_bottom = id;
    id += 1;
    let pts_list: String = bottom_pts
        .iter()
        .map(|p| format!("#{}", p))
        .collect::<Vec<_>>()
        .join(",");
    writeln!(
        out,
        "#{}=POLYLINE('bottom_outline',({}));",
        polyline_bottom, pts_list
    )
    .unwrap();

    // Polyline for top outline
    let polyline_top = id;
    id += 1;
    let pts_list: String = top_pts
        .iter()
        .map(|p| format!("#{}", p))
        .collect::<Vec<_>>()
        .join(",");
    writeln!(
        out,
        "#{}=POLYLINE('top_outline',({}));",
        polyline_top, pts_list
    )
    .unwrap();

    // Geometric representation context
    let geo_ctx = id;
    id += 1;
    writeln!(out, "#{}=GEOMETRIC_REPRESENTATION_CONTEXT(3);", geo_ctx).unwrap();

    let global_uc = id;
    id += 1;
    writeln!(
        out,
        "#{}=GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT(0.001);",
        global_uc
    )
    .unwrap();

    let repr_ctx = id;
    id += 1;
    writeln!(out, "#{}=(GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#{})) REPRESENTATION_CONTEXT('',''));", repr_ctx, global_uc).unwrap();

    // Shape representation (referencing the axis and polylines)
    // Use the pre-allocated shape_rep id
    writeln!(
        out,
        "#{}=SHAPE_REPRESENTATION('Board',(#{},#{},#{}),#{});",
        shape_rep, axis, polyline_bottom, polyline_top, repr_ctx
    )
    .unwrap();

    writeln!(out, "ENDSEC;").unwrap();
    writeln!(out, "END-ISO-10303-21;").unwrap();

    out
}
