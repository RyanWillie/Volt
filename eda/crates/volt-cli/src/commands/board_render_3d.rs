//! Board (PCB) 3D renderer.
//!
//! Generates a self-contained HTML file with an interactive Three.js 3D view
//! of the PCB board. Shows the board substrate, copper traces/pads, solder mask,
//! silkscreen, vias, holes, and component bodies.

use std::collections::HashMap;
use std::fmt::Write;
use std::path::Path;

use uuid::Uuid;

use volt_core::common::*;
use volt_core::library::{Device, Footprint, Package};
use volt_core::project::*;

use super::project_io::{self, Result};

// ===========================================================================
// Color constants (CSS hex)
// ===========================================================================

const BOARD_COLOR: &str = "#1a6b1a";
const BOARD_SIDE_COLOR: &str = "#c8a84e";
const SOLDERMASK_TOP: &str = "#0d5e0d";
const COPPER_COLOR: &str = "#c87533";
const PAD_TOP_COLOR: &str = "#c8a84e";
const VIA_COLOR: &str = "#c8a84e";
const SILK_TOP_COLOR: &str = "#e8e8e8";
const HOLE_COLOR: &str = "#222222";
const BODY_COLOR: &str = "#333333";
const BODY_PIN1_COLOR: &str = "#e8e8e8";
const BG_COLOR: &str = "#1a1a2e";

// ===========================================================================
// Board geometry constants (mm)
// ===========================================================================

const BOARD_THICKNESS: f64 = 1.6;
const COPPER_THICKNESS: f64 = 0.035;
const SILK_THICKNESS: f64 = 0.02;
const TRACE_Z_OFFSET: f64 = 0.04; // above solder mask
const PAD_Z_OFFSET: f64 = 0.05;
const VIA_RING_Z: f64 = 0.05;
const BODY_HEIGHT: f64 = 1.0; // default component body height
const BODY_Z_GAP: f64 = 0.06; // gap between board surface and body

// ===========================================================================
// Public entry point
// ===========================================================================

/// Render a board to an interactive 3D HTML file.
pub fn render_board_3d(project: &Path, board_name: &str, output: &Path) -> Result<()> {
    project_io::ensure_project(project)?;

    let board = project_io::read_board(project, board_name)?;
    let circuit = project_io::read_circuit(project)?;

    // Component designators
    let comp_name: HashMap<Uuid, &str> = circuit
        .components
        .iter()
        .map(|c| (c.uuid, c.name.as_str()))
        .collect();

    // Load packages and devices
    let mut pkg_cache: HashMap<Uuid, Package> = HashMap::new();
    let mut dev_cache: HashMap<Uuid, Device> = HashMap::new();

    for bd in &board.devices {
        if !dev_cache.contains_key(&bd.lib_device) {
            if let Ok(dev) =
                project_io::read_library_element::<Device>(project, "devices", &bd.lib_device)
            {
                if !pkg_cache.contains_key(&dev.package) {
                    if let Ok(pkg) = project_io::read_library_element::<Package>(
                        project, "packages", &dev.package,
                    ) {
                        pkg_cache.insert(dev.package, pkg);
                    }
                }
                dev_cache.insert(bd.lib_device, dev);
            }
        }
    }

    // Board outline bounds
    let (min_x, min_y, max_x, max_y) = board_outline_bounds(&board);
    let board_w = max_x - min_x;
    let board_h = max_y - min_y;
    let cx = (min_x + max_x) / 2.0;
    let cy = (min_y + max_y) / 2.0;

    // Collect outline vertices for the board shape
    let outline_verts = board_outline_vertices(&board);

    // Build the JavaScript scene data
    let mut js = String::with_capacity(32768);

    // --- Board outline shape (2D polygon for extrusion) ---
    write_board_shape(&mut js, &outline_verts, cx, cy)?;

    // --- Pads ---
    write_pads(&mut js, &board, &dev_cache, &pkg_cache)?;

    // --- Traces ---
    write_traces(&mut js, &board, &dev_cache, &pkg_cache)?;

    // --- Vias ---
    write_vias(&mut js, &board)?;

    // --- Holes ---
    write_holes(&mut js, &board)?;

    // --- Copper planes ---
    write_planes(&mut js, &board, cx, cy)?;

    // --- Component bodies ---
    write_component_bodies(&mut js, &board, &comp_name, &dev_cache, &pkg_cache)?;

    // --- Silkscreen ---
    write_silkscreen(&mut js, &board, &dev_cache, &pkg_cache)?;

    // Camera setup
    let cam_dist = board_w.max(board_h) * 1.5;

    // Assemble full HTML
    let html = format!(
        r##"<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Volt PCB 3D View — {board_name}</title>
<style>
  * {{ margin: 0; padding: 0; box-sizing: border-box; }}
  body {{ background: {BG_COLOR}; overflow: hidden; font-family: system-ui, sans-serif; }}
  canvas {{ display: block; }}
  #info {{
    position: absolute; top: 12px; left: 12px;
    color: #aaa; font-size: 13px;
    pointer-events: none; user-select: none;
  }}
  #info b {{ color: #ddd; }}
</style>
</head>
<body>
<div id="info"><b>Volt PCB 3D</b> — {board_name} ({board_w:.1}×{board_h:.1} mm)<br>
Drag to orbit · Scroll to zoom · Right-drag to pan</div>

<script type="importmap">
{{
  "imports": {{
    "three": "https://cdn.jsdelivr.net/npm/three@0.170.0/build/three.module.js",
    "three/addons/": "https://cdn.jsdelivr.net/npm/three@0.170.0/examples/jsm/"
  }}
}}
</script>

<script type="module">
import * as THREE from 'three';
import {{ OrbitControls }} from 'three/addons/controls/OrbitControls.js';

// ---- Scene setup ----
const scene = new THREE.Scene();
scene.background = new THREE.Color('{BG_COLOR}');

const camera = new THREE.PerspectiveCamera(45, innerWidth / innerHeight, 0.1, 2000);
camera.position.set(0, -{cam_dist:.1}, {cam_dist:.1});
camera.up.set(0, 0, 1);

const renderer = new THREE.WebGLRenderer({{ antialias: true }});
renderer.setSize(innerWidth, innerHeight);
renderer.setPixelRatio(devicePixelRatio);
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.2;
document.body.appendChild(renderer.domElement);

const controls = new OrbitControls(camera, renderer.domElement);
controls.target.set(0, 0, 0);
controls.enableDamping = true;
controls.dampingFactor = 0.08;
controls.minDistance = 5;
controls.maxDistance = {cam_dist:.1} * 4;
controls.update();

// ---- Lighting ----
const ambientLight = new THREE.AmbientLight(0xffffff, 0.5);
scene.add(ambientLight);

const dirLight = new THREE.DirectionalLight(0xffffff, 1.2);
dirLight.position.set({board_w:.1} * 0.8, -{board_h:.1} * 0.6, {board_w:.1} * 1.5);
dirLight.castShadow = true;
dirLight.shadow.mapSize.width = 2048;
dirLight.shadow.mapSize.height = 2048;
const shadowSize = {cam_dist:.1};
dirLight.shadow.camera.left = -shadowSize;
dirLight.shadow.camera.right = shadowSize;
dirLight.shadow.camera.top = shadowSize;
dirLight.shadow.camera.bottom = -shadowSize;
scene.add(dirLight);

const fillLight = new THREE.DirectionalLight(0xaaccff, 0.3);
fillLight.position.set(-{board_w:.1}, {board_h:.1}, {board_w:.1} * 0.5);
scene.add(fillLight);

const rimLight = new THREE.DirectionalLight(0xffeedd, 0.2);
rimLight.position.set(0, 0, -{board_w:.1});
scene.add(rimLight);

// ---- Helper functions ----
function hexColor(hex) {{ return new THREE.Color(hex); }}

function makeMat(color, opts = {{}}) {{
  return new THREE.MeshStandardMaterial({{
    color: hexColor(color),
    metalness: opts.metalness ?? 0.1,
    roughness: opts.roughness ?? 0.7,
    side: opts.side ?? THREE.FrontSide,
    ...opts,
  }});
}}

const boardMat = makeMat('{BOARD_COLOR}', {{ roughness: 0.8 }});
const boardSideMat = makeMat('{BOARD_SIDE_COLOR}', {{ metalness: 0.3, roughness: 0.5 }});
const copperMat = makeMat('{COPPER_COLOR}', {{ metalness: 0.7, roughness: 0.3 }});
const padMat = makeMat('{PAD_TOP_COLOR}', {{ metalness: 0.8, roughness: 0.2 }});
const viaMat = makeMat('{VIA_COLOR}', {{ metalness: 0.8, roughness: 0.2 }});
const holeMat = makeMat('{HOLE_COLOR}', {{ roughness: 0.9 }});
const silkMat = makeMat('{SILK_TOP_COLOR}', {{ roughness: 0.9 }});
const bodyMat = makeMat('{BODY_COLOR}', {{ roughness: 0.6 }});
const pin1Mat = makeMat('{BODY_PIN1_COLOR}', {{ roughness: 0.5 }});
const maskMat = makeMat('{SOLDERMASK_TOP}', {{ roughness: 0.85, transparent: true, opacity: 0.6 }});

const boardZ = 0;
const topZ = {board_tz:.4};
const botZ = {board_bz:.4};

// ---- Board substrate ----
{js}

// ---- Animate ----
function animate() {{
  requestAnimationFrame(animate);
  controls.update();
  renderer.render(scene, camera);
}}
animate();

window.addEventListener('resize', () => {{
  camera.aspect = innerWidth / innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(innerWidth, innerHeight);
}});
</script>
</body>
</html>"##,
        board_name = escape_html(board_name),
        board_w = board_w,
        board_h = board_h,
        cam_dist = cam_dist,
        board_tz = BOARD_THICKNESS / 2.0 + COPPER_THICKNESS,
        board_bz = -(BOARD_THICKNESS / 2.0 + COPPER_THICKNESS),
    );

    std::fs::write(output, &html)?;

    let result = serde_json::json!({
        "status": "ok",
        "format": "3d_html",
        "output": output.display().to_string(),
        "devices": board.devices.len(),
        "traces": board.net_segments.iter().map(|s| s.traces.len()).sum::<usize>(),
        "planes": board.planes.len(),
        "vias": board.net_segments.iter().map(|s| s.vias.len()).sum::<usize>(),
        "holes": board.holes.len(),
        "board_size": { "width": board_w, "height": board_h },
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

// ===========================================================================
// Board outline
// ===========================================================================

fn board_outline_bounds(board: &Board) -> (f64, f64, f64, f64) {
    let mut min_x = f64::INFINITY;
    let mut min_y = f64::INFINITY;
    let mut max_x = f64::NEG_INFINITY;
    let mut max_y = f64::NEG_INFINITY;

    for poly in &board.polygons {
        if poly.layer == Layer::BoardOutlines {
            for v in &poly.vertices {
                min_x = min_x.min(v.position.x);
                min_y = min_y.min(v.position.y);
                max_x = max_x.max(v.position.x);
                max_y = max_y.max(v.position.y);
            }
        }
    }

    if min_x == f64::INFINITY {
        (0.0, 0.0, 100.0, 100.0)
    } else {
        (min_x, min_y, max_x, max_y)
    }
}

fn board_outline_vertices(board: &Board) -> Vec<(f64, f64)> {
    for poly in &board.polygons {
        if poly.layer == Layer::BoardOutlines && poly.vertices.len() >= 3 {
            return poly
                .vertices
                .iter()
                .map(|v| (v.position.x, v.position.y))
                .collect();
        }
    }
    // Fallback rectangle
    vec![
        (0.0, 0.0),
        (100.0, 0.0),
        (100.0, 100.0),
        (0.0, 100.0),
        (0.0, 0.0),
    ]
}

// ===========================================================================
// JS generation: board shape
// ===========================================================================

fn write_board_shape(
    js: &mut String,
    outline: &[(f64, f64)],
    cx: f64,
    cy: f64,
) -> std::fmt::Result {
    // Create a THREE.Shape from outline vertices (centered at origin)
    writeln!(js, "// Board substrate")?;
    writeln!(js, "{{")?;
    writeln!(js, "  const shape = new THREE.Shape();")?;

    for (i, (x, y)) in outline.iter().enumerate() {
        let lx = x - cx;
        let ly = y - cy;
        if i == 0 {
            writeln!(js, "  shape.moveTo({lx:.4}, {ly:.4});")?;
        } else {
            writeln!(js, "  shape.lineTo({lx:.4}, {ly:.4});")?;
        }
    }

    writeln!(
        js,
        "  const extrudeSettings = {{ depth: {BOARD_THICKNESS}, bevelEnabled: false }};"
    )?;
    writeln!(
        js,
        "  const boardGeo = new THREE.ExtrudeGeometry(shape, extrudeSettings);"
    )?;
    // Rotate so Z is up (extrude goes along Z, but we want board flat in XY plane with Z up)
    // The extrude goes along +Z by default, which is what we want.
    // But we need to center it vertically.
    writeln!(
        js,
        "  boardGeo.translate(0, 0, {:.4});",
        -BOARD_THICKNESS / 2.0
    )?;

    // Use an array of materials: [sides, top, bottom] via groups
    writeln!(js, "  const boardMesh = new THREE.Mesh(boardGeo, [boardSideMat, boardMat, boardMat]);")?;
    writeln!(js, "  boardMesh.receiveShadow = true;")?;
    writeln!(js, "  boardMesh.castShadow = true;")?;
    writeln!(js, "  scene.add(boardMesh);")?;
    writeln!(js, "}}")?;

    Ok(())
}

// ===========================================================================
// JS generation: pads
// ===========================================================================

fn write_pads(
    js: &mut String,
    board: &Board,
    dev_cache: &HashMap<Uuid, Device>,
    pkg_cache: &HashMap<Uuid, Package>,
) -> std::fmt::Result {
    let (cx, cy) = board_center(board);

    writeln!(js, "\n// Pads")?;
    for bd in &board.devices {
        let footprint = match get_footprint(bd, dev_cache, pkg_cache) {
            Some(f) => f,
            None => continue,
        };

        for fp_pad in &footprint.pads {
            let (wx, wy) = transform_point(fp_pad.position.x, fp_pad.position.y, bd);
            let lx = wx - cx;
            let ly = wy - cy;

            let is_tht = fp_pad.side == PadSide::ThroughHole;
            let on_bottom = bd.flip && fp_pad.side == PadSide::Top
                || !bd.flip && fp_pad.side == PadSide::Bottom;
            let z = if on_bottom {
                -(BOARD_THICKNESS / 2.0 + PAD_Z_OFFSET)
            } else {
                BOARD_THICKNESS / 2.0 + PAD_Z_OFFSET
            };

            let w = fp_pad.width;
            let h = fp_pad.height;

            match fp_pad.shape {
                PadShape::Round => {
                    let r = w.max(h) / 2.0;
                    writeln!(js, "{{")?;
                    writeln!(
                        js,
                        "  const geo = new THREE.CylinderGeometry({r:.4}, {r:.4}, {PAD_Z_OFFSET}, 24);"
                    )?;
                    writeln!(js, "  geo.rotateX(Math.PI / 2);")?;
                    writeln!(js, "  const mesh = new THREE.Mesh(geo, padMat);")?;
                    writeln!(js, "  mesh.position.set({lx:.4}, {ly:.4}, {z:.4});")?;
                    writeln!(js, "  mesh.castShadow = true;")?;
                    writeln!(js, "  scene.add(mesh);")?;
                    writeln!(js, "}}")?;
                }
                PadShape::RoundRect | PadShape::Custom => {
                    let total_rot = if bd.flip {
                        -fp_pad.rotation.0 + bd.rotation.0
                    } else {
                        fp_pad.rotation.0 + bd.rotation.0
                    };
                    writeln!(js, "{{")?;
                    writeln!(
                        js,
                        "  const geo = new THREE.BoxGeometry({w:.4}, {h:.4}, {PAD_Z_OFFSET});"
                    )?;
                    writeln!(js, "  const mesh = new THREE.Mesh(geo, padMat);")?;
                    writeln!(js, "  mesh.position.set({lx:.4}, {ly:.4}, {z:.4});")?;
                    if total_rot.abs() > 0.01 {
                        writeln!(
                            js,
                            "  mesh.rotation.z = {:.4};",
                            -total_rot.to_radians()
                        )?;
                    }
                    writeln!(js, "  mesh.castShadow = true;")?;
                    writeln!(js, "  scene.add(mesh);")?;
                    writeln!(js, "}}")?;
                }
            }

            // THT drill hole visual
            if is_tht {
                for hole in &fp_pad.holes {
                    let drill_r = hole.diameter / 2.0;
                    writeln!(js, "{{")?;
                    writeln!(
                        js,
                        "  const geo = new THREE.CylinderGeometry({drill_r:.4}, {drill_r:.4}, {:.4}, 16);",
                        BOARD_THICKNESS + 0.2
                    )?;
                    writeln!(js, "  geo.rotateX(Math.PI / 2);")?;
                    writeln!(js, "  const mesh = new THREE.Mesh(geo, holeMat);")?;
                    writeln!(js, "  mesh.position.set({lx:.4}, {ly:.4}, 0);")?;
                    writeln!(js, "  scene.add(mesh);")?;
                    writeln!(js, "}}")?;
                }
            }
        }
    }
    Ok(())
}

// ===========================================================================
// JS generation: traces
// ===========================================================================

fn write_traces(
    js: &mut String,
    board: &Board,
    dev_cache: &HashMap<Uuid, Device>,
    pkg_cache: &HashMap<Uuid, Package>,
) -> std::fmt::Result {
    let (cx, cy) = board_center(board);

    writeln!(js, "\n// Traces")?;
    for seg in &board.net_segments {
        for trace in &seg.traces {
            let from = resolve_trace_pos(&trace.from, board, &seg.junctions, dev_cache, pkg_cache);
            let to = resolve_trace_pos(&trace.to, board, &seg.junctions, dev_cache, pkg_cache);

            let (fx, fy) = match from {
                Some(p) => (p.0 - cx, p.1 - cy),
                None => continue,
            };
            let (tx, ty) = match to {
                Some(p) => (p.0 - cx, p.1 - cy),
                None => continue,
            };

            let is_bottom = matches!(trace.layer, Layer::BottomCopper);
            let z = if is_bottom {
                -(BOARD_THICKNESS / 2.0 + TRACE_Z_OFFSET)
            } else {
                BOARD_THICKNESS / 2.0 + TRACE_Z_OFFSET
            };

            let dx = tx - fx;
            let dy = ty - fy;
            let len = (dx * dx + dy * dy).sqrt();
            if len < 0.001 {
                continue;
            }
            let angle = dy.atan2(dx);
            let mid_x = (fx + tx) / 2.0;
            let mid_y = (fy + ty) / 2.0;
            let w = trace.width;

            writeln!(js, "{{")?;
            writeln!(
                js,
                "  const geo = new THREE.BoxGeometry({len:.4}, {w:.4}, {COPPER_THICKNESS});"
            )?;
            writeln!(js, "  const mesh = new THREE.Mesh(geo, copperMat);")?;
            writeln!(
                js,
                "  mesh.position.set({mid_x:.4}, {mid_y:.4}, {z:.4});"
            )?;
            writeln!(js, "  mesh.rotation.z = {:.4};", -angle)?;
            writeln!(js, "  mesh.castShadow = true;")?;
            writeln!(js, "  scene.add(mesh);")?;
            writeln!(js, "}}")?;
        }
    }
    Ok(())
}

// ===========================================================================
// JS generation: vias
// ===========================================================================

fn write_vias(js: &mut String, board: &Board) -> std::fmt::Result {
    let (cx, cy) = board_center(board);

    writeln!(js, "\n// Vias")?;
    for seg in &board.net_segments {
        for via in &seg.vias {
            let lx = via.position.x - cx;
            let ly = via.position.y - cy;
            let drill_r = via.drill / 2.0;
            let outer_r = drill_r + 0.2; // annular ring

            // Via ring (top)
            writeln!(js, "{{")?;
            writeln!(
                js,
                "  const geo = new THREE.CylinderGeometry({outer_r:.4}, {outer_r:.4}, {VIA_RING_Z}, 20);"
            )?;
            writeln!(js, "  geo.rotateX(Math.PI / 2);")?;
            writeln!(js, "  const mesh = new THREE.Mesh(geo, viaMat);")?;
            let via_top_z = BOARD_THICKNESS / 2.0 + VIA_RING_Z / 2.0;
            writeln!(js, "  mesh.position.set({lx:.4}, {ly:.4}, {via_top_z:.4});")?;
            writeln!(js, "  mesh.castShadow = true;")?;
            writeln!(js, "  scene.add(mesh);")?;
            writeln!(js, "}}")?;

            // Via ring (bottom)
            writeln!(js, "{{")?;
            writeln!(
                js,
                "  const geo = new THREE.CylinderGeometry({outer_r:.4}, {outer_r:.4}, {VIA_RING_Z}, 20);"
            )?;
            writeln!(js, "  geo.rotateX(Math.PI / 2);")?;
            writeln!(js, "  const mesh = new THREE.Mesh(geo, viaMat);")?;
            let via_bot_z = -(BOARD_THICKNESS / 2.0 + VIA_RING_Z / 2.0);
            writeln!(
                js,
                "  mesh.position.set({lx:.4}, {ly:.4}, {via_bot_z:.4});"
            )?;
            writeln!(js, "  scene.add(mesh);")?;
            writeln!(js, "}}")?;

            // Drill hole
            writeln!(js, "{{")?;
            writeln!(
                js,
                "  const geo = new THREE.CylinderGeometry({drill_r:.4}, {drill_r:.4}, {:.4}, 16);",
                BOARD_THICKNESS + 0.4
            )?;
            writeln!(js, "  geo.rotateX(Math.PI / 2);")?;
            writeln!(js, "  const mesh = new THREE.Mesh(geo, holeMat);")?;
            writeln!(js, "  mesh.position.set({lx:.4}, {ly:.4}, 0);")?;
            writeln!(js, "  scene.add(mesh);")?;
            writeln!(js, "}}")?;
        }
    }
    Ok(())
}

// ===========================================================================
// JS generation: holes
// ===========================================================================

fn write_holes(js: &mut String, board: &Board) -> std::fmt::Result {
    let (cx, cy) = board_center(board);

    writeln!(js, "\n// Non-plated holes")?;
    for hole in &board.holes {
        if let Some(v) = hole.path.first() {
            let lx = v.position.x - cx;
            let ly = v.position.y - cy;
            let r = hole.diameter / 2.0;

            writeln!(js, "{{")?;
            writeln!(
                js,
                "  const geo = new THREE.CylinderGeometry({r:.4}, {r:.4}, {:.4}, 24);",
                BOARD_THICKNESS + 0.4
            )?;
            writeln!(js, "  geo.rotateX(Math.PI / 2);")?;
            writeln!(js, "  const mesh = new THREE.Mesh(geo, holeMat);")?;
            writeln!(js, "  mesh.position.set({lx:.4}, {ly:.4}, 0);")?;
            writeln!(js, "  scene.add(mesh);")?;
            writeln!(js, "}}")?;
        }
    }
    Ok(())
}

// ===========================================================================
// JS generation: copper planes
// ===========================================================================

fn write_planes(
    js: &mut String,
    board: &Board,
    cx: f64,
    cy: f64,
) -> std::fmt::Result {
    writeln!(js, "\n// Copper planes")?;
    for plane in &board.planes {
        if plane.vertices.len() < 3 {
            continue;
        }

        let is_bottom = matches!(plane.layer, Layer::BottomCopper);
        let z = if is_bottom {
            -(BOARD_THICKNESS / 2.0 + COPPER_THICKNESS / 2.0)
        } else {
            BOARD_THICKNESS / 2.0 + COPPER_THICKNESS / 2.0
        };

        writeln!(js, "{{")?;
        writeln!(js, "  const shape = new THREE.Shape();")?;
        for (i, v) in plane.vertices.iter().enumerate() {
            let lx = v.position.x - cx;
            let ly = v.position.y - cy;
            if i == 0 {
                writeln!(js, "  shape.moveTo({lx:.4}, {ly:.4});")?;
            } else {
                writeln!(js, "  shape.lineTo({lx:.4}, {ly:.4});")?;
            }
        }
        writeln!(
            js,
            "  const geo = new THREE.ExtrudeGeometry(shape, {{ depth: {COPPER_THICKNESS}, bevelEnabled: false }});"
        )?;
        writeln!(js, "  geo.translate(0, 0, {:.4});", z - COPPER_THICKNESS / 2.0)?;
        writeln!(js, "  const mesh = new THREE.Mesh(geo, copperMat);")?;
        writeln!(js, "  mesh.castShadow = true;")?;
        writeln!(js, "  scene.add(mesh);")?;
        writeln!(js, "}}")?;
    }
    Ok(())
}

// ===========================================================================
// JS generation: component bodies
// ===========================================================================

fn write_component_bodies(
    js: &mut String,
    board: &Board,
    comp_name: &HashMap<Uuid, &str>,
    dev_cache: &HashMap<Uuid, Device>,
    pkg_cache: &HashMap<Uuid, Package>,
) -> std::fmt::Result {
    let (cx, cy) = board_center(board);

    writeln!(js, "\n// Component bodies")?;
    for bd in &board.devices {
        let footprint = match get_footprint(bd, dev_cache, pkg_cache) {
            Some(f) => f,
            None => continue,
        };

        // Compute the body bounding box from pads
        let mut bmin_x = f64::INFINITY;
        let mut bmin_y = f64::INFINITY;
        let mut bmax_x = f64::NEG_INFINITY;
        let mut bmax_y = f64::NEG_INFINITY;

        for fp_pad in &footprint.pads {
            let px = fp_pad.position.x;
            let py = fp_pad.position.y;
            let hw = fp_pad.width / 2.0;
            let hh = fp_pad.height / 2.0;
            bmin_x = bmin_x.min(px - hw);
            bmin_y = bmin_y.min(py - hh);
            bmax_x = bmax_x.max(px + hw);
            bmax_y = bmax_y.max(py + hh);
        }

        // Also check courtyard/outline polygons
        for poly in &footprint.polygons {
            if matches!(
                poly.layer,
                Layer::TopCourtyard | Layer::BottomCourtyard
                    | Layer::TopPackageOutlines | Layer::BottomPackageOutlines
                    | Layer::TopDocumentation | Layer::BottomDocumentation
            ) {
                for v in &poly.vertices {
                    bmin_x = bmin_x.min(v.position.x);
                    bmin_y = bmin_y.min(v.position.y);
                    bmax_x = bmax_x.max(v.position.x);
                    bmax_y = bmax_y.max(v.position.y);
                }
            }
        }

        if bmin_x == f64::INFINITY {
            continue; // no geometry
        }

        // Shrink body slightly inside the pad extents
        let body_margin = 0.1;
        let bw = (bmax_x - bmin_x) - body_margin * 2.0;
        let bh = (bmax_y - bmin_y) - body_margin * 2.0;
        if bw <= 0.0 || bh <= 0.0 {
            continue;
        }

        let body_cx = (bmin_x + bmax_x) / 2.0;
        let body_cy = (bmin_y + bmax_y) / 2.0;

        // Transform body center to world coords
        let (wcx, wcy) = transform_point(body_cx, body_cy, bd);
        let lx = wcx - cx;
        let ly = wcy - cy;

        let on_bottom = bd.flip;
        let body_h = BODY_HEIGHT.min(bw * 0.8).min(bh * 0.8).max(0.3);
        let z = if on_bottom {
            -(BOARD_THICKNESS / 2.0 + BODY_Z_GAP + body_h / 2.0)
        } else {
            BOARD_THICKNESS / 2.0 + BODY_Z_GAP + body_h / 2.0
        };

        let total_rot = if bd.flip {
            bd.rotation.0
        } else {
            bd.rotation.0
        };

        let name = comp_name
            .get(&bd.component)
            .copied()
            .unwrap_or("?");

        writeln!(js, "// {name}")?;
        writeln!(js, "{{")?;
        writeln!(
            js,
            "  const geo = new THREE.BoxGeometry({bw:.4}, {bh:.4}, {body_h:.4});"
        )?;
        writeln!(js, "  const mesh = new THREE.Mesh(geo, bodyMat);")?;
        writeln!(js, "  mesh.position.set({lx:.4}, {ly:.4}, {z:.4});")?;
        if total_rot.abs() > 0.01 {
            writeln!(
                js,
                "  mesh.rotation.z = {:.4};",
                -total_rot.to_radians()
            )?;
        }
        writeln!(js, "  mesh.castShadow = true;")?;
        writeln!(js, "  mesh.receiveShadow = true;")?;
        writeln!(js, "  scene.add(mesh);")?;

        // Pin 1 marker (small dot on top of body)
        if let Some(first_pad) = footprint.pads.first() {
            let (p1x, p1y) = transform_point(first_pad.position.x, first_pad.position.y, bd);
            let p1lx = p1x - cx;
            let p1ly = p1y - cy;
            let marker_z = if on_bottom {
                z - body_h / 2.0 - 0.02
            } else {
                z + body_h / 2.0 + 0.02
            };
            writeln!(
                js,
                "  const markerGeo = new THREE.SphereGeometry(0.2, 8, 8);"
            )?;
            writeln!(
                js,
                "  const marker = new THREE.Mesh(markerGeo, pin1Mat);"
            )?;
            writeln!(
                js,
                "  marker.position.set({p1lx:.4}, {p1ly:.4}, {marker_z:.4});"
            )?;
            writeln!(js, "  scene.add(marker);")?;
        }

        writeln!(js, "}}")?;
    }
    Ok(())
}

// ===========================================================================
// JS generation: silkscreen
// ===========================================================================

fn write_silkscreen(
    js: &mut String,
    board: &Board,
    dev_cache: &HashMap<Uuid, Device>,
    pkg_cache: &HashMap<Uuid, Package>,
) -> std::fmt::Result {
    let (cx, cy) = board_center(board);

    writeln!(js, "\n// Silkscreen")?;
    for bd in &board.devices {
        let footprint = match get_footprint(bd, dev_cache, pkg_cache) {
            Some(f) => f,
            None => continue,
        };

        for poly in &footprint.polygons {
            if !(poly.layer == Layer::TopLegend || poly.layer == Layer::BottomLegend) {
                continue;
            }
            if poly.vertices.len() < 2 {
                continue;
            }

            let is_bottom = poly.layer == Layer::BottomLegend;
            let z = if is_bottom {
                -(BOARD_THICKNESS / 2.0 + SILK_THICKNESS)
            } else {
                BOARD_THICKNESS / 2.0 + SILK_THICKNESS
            };

            // Transform vertices
            let points: Vec<(f64, f64)> = poly
                .vertices
                .iter()
                .map(|v| {
                    let (wx, wy) = transform_point(v.position.x, v.position.y, bd);
                    (wx - cx, wy - cy)
                })
                .collect();

            // Draw as line segments
            for i in 0..points.len().saturating_sub(1) {
                let (ax, ay) = points[i];
                let (bx, by) = points[i + 1];
                let dx = bx - ax;
                let dy = by - ay;
                let len = (dx * dx + dy * dy).sqrt();
                if len < 0.001 {
                    continue;
                }
                let angle = dy.atan2(dx);
                let mid_x = (ax + bx) / 2.0;
                let mid_y = (ay + by) / 2.0;
                let w = poly.width.max(0.12);

                writeln!(js, "{{")?;
                writeln!(
                    js,
                    "  const geo = new THREE.BoxGeometry({len:.4}, {w:.4}, {SILK_THICKNESS});"
                )?;
                writeln!(js, "  const mesh = new THREE.Mesh(geo, silkMat);")?;
                writeln!(
                    js,
                    "  mesh.position.set({mid_x:.4}, {mid_y:.4}, {z:.4});"
                )?;
                writeln!(js, "  mesh.rotation.z = {:.4};", -angle)?;
                writeln!(js, "  scene.add(mesh);")?;
                writeln!(js, "}}")?;
            }
        }
    }
    Ok(())
}

// ===========================================================================
// Shared helpers
// ===========================================================================

fn board_center(board: &Board) -> (f64, f64) {
    let (min_x, min_y, max_x, max_y) = board_outline_bounds(board);
    ((min_x + max_x) / 2.0, (min_y + max_y) / 2.0)
}

fn transform_point(px: f64, py: f64, bd: &BoardDevice) -> (f64, f64) {
    let mut lx = px;
    let ly = py;
    if bd.flip {
        lx = -lx;
    }
    let theta = bd.rotation.0.to_radians();
    let cos_t = theta.cos();
    let sin_t = theta.sin();
    let rx = lx * cos_t - ly * sin_t;
    let ry = lx * sin_t + ly * cos_t;
    (bd.position.x + rx, bd.position.y + ry)
}

fn get_footprint<'a>(
    bd: &BoardDevice,
    dev_cache: &HashMap<Uuid, Device>,
    pkg_cache: &'a HashMap<Uuid, Package>,
) -> Option<&'a Footprint> {
    let dev = dev_cache.get(&bd.lib_device)?;
    let pkg = pkg_cache.get(&dev.package)?;
    pkg.footprints
        .iter()
        .find(|f| f.uuid == bd.lib_footprint)
        .or(pkg.footprints.first())
}

fn resolve_trace_pos(
    ep: &TraceEndpoint,
    board: &Board,
    local_junctions: &[Junction],
    dev_cache: &HashMap<Uuid, Device>,
    pkg_cache: &HashMap<Uuid, Package>,
) -> Option<(f64, f64)> {
    match ep {
        TraceEndpoint::Device { device, pad } => {
            let bd = board.devices.iter().find(|d| d.component == *device)?;
            let footprint = get_footprint(bd, dev_cache, pkg_cache)?;
            let fp_pad = footprint.pads.iter().find(|p| p.uuid == *pad)?;
            Some(transform_point(fp_pad.position.x, fp_pad.position.y, bd))
        }
        TraceEndpoint::Junction { junction } => {
            if let Some(j) = local_junctions.iter().find(|j| j.uuid == *junction) {
                return Some((j.position.x, j.position.y));
            }
            for seg in &board.net_segments {
                if let Some(j) = seg.junctions.iter().find(|j| j.uuid == *junction) {
                    return Some((j.position.x, j.position.y));
                }
            }
            None
        }
        TraceEndpoint::Via { via } => {
            for seg in &board.net_segments {
                if let Some(v) = seg.vias.iter().find(|v| v.uuid == *via) {
                    return Some((v.position.x, v.position.y));
                }
            }
            None
        }
    }
}

fn escape_html(s: &str) -> String {
    s.replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
        .replace('"', "&quot;")
}
