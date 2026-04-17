//! Interactive HTML BOM export.
//!
//! Generates a self-contained HTML file with an embedded BOM table and
//! clickable 2D board outline. Hover/click a BOM row to highlight the
//! component on the board, and vice versa.

use std::collections::HashMap;
use std::fmt::Write;

use uuid::Uuid;

use volt_core::common::*;
use volt_core::library::{Device, Package};
use volt_core::project::*;

use crate::bom::{self, BomLibrary};
use crate::gerber::{BoardLibrary, transform_point};

/// Generate a self-contained interactive HTML BOM.
pub fn export_interactive_html_bom(
    board: &Board,
    circuit: &Circuit,
    library: &(impl BoardLibrary + BomLibrary),
) -> String {
    let bom_result = bom::generate_bom(circuit, library);

    // Collect component placements
    let mut placements: Vec<serde_json::Value> = Vec::new();
    let comp_names: HashMap<Uuid, String> = circuit
        .components
        .iter()
        .map(|c| (c.uuid, c.name.clone()))
        .collect();

    for dev in &board.devices {
        let designator = comp_names
            .get(&dev.component)
            .map(|s| s.as_str())
            .unwrap_or("?");
        let side = if dev.flip { "bottom" } else { "top" };

        // Get pad positions for highlighting
        let mut pads = Vec::new();
        if let Some(lib_dev) = BoardLibrary::get_device(library, &dev.lib_device) {
            if let Some(pkg) = BoardLibrary::get_package(library, &lib_dev.package) {
                if let Some(fp) = pkg
                    .footprints
                    .iter()
                    .find(|f| f.uuid == dev.lib_footprint)
                    .or_else(|| pkg.footprints.first())
                {
                    for fp_pad in &fp.pads {
                        let (x, y) = transform_point(fp_pad.position.x, fp_pad.position.y, dev);
                        pads.push(serde_json::json!({
                            "x": x, "y": y,
                            "w": fp_pad.width, "h": fp_pad.height,
                        }));
                    }
                }
            }
        }

        placements.push(serde_json::json!({
            "ref": designator,
            "x": dev.position.x,
            "y": dev.position.y,
            "rotation": dev.rotation.0,
            "side": side,
            "pads": pads,
        }));
    }

    // Board outline
    let outline: Vec<[f64; 2]> = board
        .polygons
        .iter()
        .find(|p| p.layer == Layer::BoardOutlines)
        .map(|p| {
            p.vertices
                .iter()
                .map(|v| [v.position.x, v.position.y])
                .collect()
        })
        .unwrap_or_default();

    // BOM as JSON
    let bom_json = serde_json::json!({
        "entries": bom_result.entries.iter().map(|e| serde_json::json!({
            "designators": e.designators,
            "value": e.value,
            "package": e.package,
            "mpn": e.mpn,
            "manufacturer": e.manufacturer,
            "quantity": e.quantity,
        })).collect::<Vec<_>>(),
        "total_components": bom_result.total_components,
        "unique_parts": bom_result.unique_parts,
    });

    let data_json = serde_json::json!({
        "bom": bom_json,
        "placements": placements,
        "outline": outline,
        "board_name": board.name,
    });

    let data_str = serde_json::to_string(&data_json).unwrap_or_default();

    format!(
        r##"<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>Interactive BOM - {board_name}</title>
<style>
body {{ font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; margin: 0; padding: 16px; background: #1a1a2e; color: #e0e0e0; }}
h1 {{ font-size: 18px; margin: 0 0 12px 0; color: #64ffda; }}
.container {{ display: flex; gap: 16px; flex-wrap: wrap; }}
.board-view {{ flex: 1; min-width: 400px; background: #16213e; border-radius: 8px; padding: 12px; }}
.bom-table-wrap {{ flex: 1; min-width: 400px; background: #16213e; border-radius: 8px; padding: 12px; max-height: 80vh; overflow-y: auto; }}
svg {{ width: 100%; height: auto; }}
table {{ width: 100%; border-collapse: collapse; font-size: 13px; }}
th {{ text-align: left; padding: 6px 8px; border-bottom: 2px solid #333; color: #64ffda; position: sticky; top: 0; background: #16213e; }}
td {{ padding: 5px 8px; border-bottom: 1px solid #222; }}
tr:hover, tr.highlight {{ background: #0f3460; cursor: pointer; }}
.pad {{ fill: #4a9; stroke: none; opacity: 0.5; }}
.pad.highlight {{ fill: #64ffda; opacity: 0.9; }}
.outline {{ fill: none; stroke: #445; stroke-width: 0.3; }}
.comp-marker {{ fill: #888; stroke: none; opacity: 0.4; }}
.comp-marker.highlight {{ fill: #64ffda; opacity: 0.8; }}
input {{ width: 100%; padding: 6px 8px; margin-bottom: 8px; background: #0f3460; border: 1px solid #333; color: #e0e0e0; border-radius: 4px; box-sizing: border-box; }}
</style>
</head>
<body>
<h1>Interactive BOM \u2014 {board_name}</h1>
<div class="container">
<div class="board-view" id="boardView"></div>
<div class="bom-table-wrap">
<input type="text" id="search" placeholder="Search components...">
<table>
<thead><tr><th>Ref</th><th>Value</th><th>Package</th><th>MPN</th><th>Qty</th></tr></thead>
<tbody id="bomBody"></tbody>
</table>
</div>
</div>
<script>
const DATA = {data};
function init() {{
  const bom = DATA.bom;
  const placements = DATA.placements;
  const outline = DATA.outline;
  // Build SVG
  let minX=Infinity,minY=Infinity,maxX=-Infinity,maxY=-Infinity;
  outline.forEach(p => {{ minX=Math.min(minX,p[0]); minY=Math.min(minY,p[1]); maxX=Math.max(maxX,p[0]); maxY=Math.max(maxY,p[1]); }});
  const pad=2; minX-=pad; minY-=pad; maxX+=pad; maxY+=pad;
  const w=maxX-minX, h=maxY-minY;
  let svg = `<svg viewBox="${{minX}} ${{minY}} ${{w}} ${{h}}" xmlns="http://www.w3.org/2000/svg">`;
  svg += `<polygon class="outline" points="${{outline.map(p=>p[0]+','+p[1]).join(' ')}}"/>`;
  placements.forEach(pl => {{
    svg += `<circle class="comp-marker" data-ref="${{pl.ref}}" cx="${{pl.x}}" cy="${{pl.y}}" r="0.8"/>`;
    pl.pads.forEach(pd => {{
      svg += `<rect class="pad" data-ref="${{pl.ref}}" x="${{pd.x-pd.w/2}}" y="${{pd.y-pd.h/2}}" width="${{pd.w}}" height="${{pd.h}}"/>`;
    }});
  }});
  svg += '</svg>';
  document.getElementById('boardView').innerHTML = svg;
  // Build BOM table
  const tbody = document.getElementById('bomBody');
  bom.entries.forEach(e => {{
    const tr = document.createElement('tr');
    tr.dataset.refs = e.designators.join(',');
    tr.innerHTML = `<td>${{e.designators.join(', ')}}</td><td>${{e.value}}</td><td>${{e.package}}</td><td>${{e.mpn}}</td><td>${{e.quantity}}</td>`;
    tr.addEventListener('mouseenter', () => highlight(e.designators));
    tr.addEventListener('mouseleave', () => clearHighlight());
    tbody.appendChild(tr);
  }});
  // SVG hover
  document.querySelectorAll('[data-ref]').forEach(el => {{
    el.addEventListener('mouseenter', () => highlight([el.dataset.ref]));
    el.addEventListener('mouseleave', () => clearHighlight());
  }});
  // Search
  document.getElementById('search').addEventListener('input', e => {{
    const q = e.target.value.toLowerCase();
    tbody.querySelectorAll('tr').forEach(tr => {{
      tr.style.display = tr.textContent.toLowerCase().includes(q) ? '' : 'none';
    }});
  }});
}}
function highlight(refs) {{
  clearHighlight();
  document.querySelectorAll('[data-ref]').forEach(el => {{
    if (refs.includes(el.dataset.ref)) el.classList.add('highlight');
  }});
  document.querySelectorAll('#bomBody tr').forEach(tr => {{
    const trRefs = tr.dataset.refs.split(',');
    if (refs.some(r => trRefs.includes(r))) tr.classList.add('highlight');
  }});
}}
function clearHighlight() {{
  document.querySelectorAll('.highlight').forEach(el => el.classList.remove('highlight'));
}}
init();
</script>
</body>
</html>"##,
        board_name = board.name,
        data = data_str,
    )
}
