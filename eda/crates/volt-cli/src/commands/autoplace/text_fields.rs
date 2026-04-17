//! Phase 7: Text Fields — collision-aware NAME/VALUE placement.
//!
//! Builds `SchematicText` entries for each placed symbol by rotating the
//! library template offsets into world coordinates, then resolves overlaps
//! between text fields and component bodies.

use volt_core::common::*;
use volt_core::library::Symbol;
use volt_core::project::*;

use super::types::{BBox, GRID};

// ===========================================================================
// Public API
// ===========================================================================

/// Create text fields for a placed symbol from its library template.
///
/// `px`, `py` are the symbol's world position in mm.  `rotation` is the
/// symbol rotation in degrees.  Each [`SymbolText`] in the library symbol is
/// transformed into a [`SchematicText`] with its offset rotated and translated
/// to match the placement.
///
/// Template placeholders like `{{NAME}}` and `{{VALUE}}` are kept as-is;
/// they are resolved later by the serialiser.
pub fn build_symbol_texts(
    px: f64,
    py: f64,
    rotation: f64,
    lib_sym: &Symbol,
    _comp_name: &str,
    _comp_value: &str,
) -> Vec<SchematicText> {
    // Compute body extent from polygons for smart text placement.
    let body_extent = lib_sym
        .polygons
        .iter()
        .flat_map(|p| p.vertices.iter())
        .map(|v| v.position.x.abs().max(v.position.y.abs()))
        .fold(3.0_f64, f64::max);

    let is_sideways = {
        let norm = ((rotation % 360.0) + 360.0) % 360.0;
        (norm > 45.0 && norm < 135.0) || (norm > 225.0 && norm < 315.0)
    };

    lib_sym
        .texts
        .iter()
        .enumerate()
        .map(|(idx, template)| {
            let (final_pos, final_rotation) = if is_sideways {
                // For 90°/270° rotation: place text to the right of body, horizontal.
                let x_offset = body_extent + GRID;
                let y_offset = idx as f64 * (template.height * 1.5 + 0.5);
                (Position::new(px + x_offset, py + y_offset), Angle(0.0))
            } else {
                // Normal: use rotated template offset.
                let rot_rad = rotation.to_radians();
                let tx = template.position.x * rot_rad.cos() - template.position.y * rot_rad.sin();
                let ty = template.position.x * rot_rad.sin() + template.position.y * rot_rad.cos();
                (
                    Position::new(px + tx, py + ty),
                    Angle(template.rotation.0 + rotation),
                )
            };

            let align = if is_sideways {
                Alignment {
                    h: HAlign::Left,
                    v: VAlign::Bottom,
                }
            } else {
                template.align
            };

            SchematicText {
                uuid: new_uuid(),
                layer: template.layer,
                value: template.value.clone(),
                position: final_pos,
                rotation: final_rotation,
                height: template.height,
                align,
                lock: template.lock,
            }
        })
        .collect()
}

/// Resolve text-vs-component and text-vs-text collisions.
///
/// For each symbol text that overlaps a component bounding box the text is
/// flipped to the opposite side of its parent symbol.  Remaining text-on-text
/// overlaps between *different* symbols are resolved by nudging the second
/// text downward.
pub fn resolve_collisions(schematic: &mut Schematic, comp_boxes: &[BBox]) {
    // --- Pass 1: text vs component body ---
    for sym in &mut schematic.symbols {
        let sym_y = sym.position.y;

        for text in &mut sym.texts {
            if text.height <= 0.0 {
                continue;
            }

            let bbox = estimate_text_bbox(text);

            let overlapping = comp_boxes.iter().any(|cb| bbox.overlaps(cb));
            if !overlapping {
                continue;
            }

            // Flip: move text to opposite side of the symbol centre.
            if text.position.y < sym_y {
                // Text is above symbol centre → move below.
                let nearest_bottom = comp_boxes
                    .iter()
                    .filter(|cb| bbox.overlaps(cb))
                    .map(|cb| cb.y1)
                    .fold(f64::NEG_INFINITY, f64::max);
                text.position.y = nearest_bottom + GRID;
            } else {
                // Text is below (or at) symbol centre → move above.
                let nearest_top = comp_boxes
                    .iter()
                    .filter(|cb| bbox.overlaps(cb))
                    .map(|cb| cb.y0)
                    .fold(f64::INFINITY, f64::min);
                text.position.y = nearest_top - GRID;
            }

            // Re-check after flip and nudge further if still overlapping.
            let bbox2 = estimate_text_bbox(text);
            if comp_boxes.iter().any(|cb| bbox2.overlaps(cb)) {
                // Nudge one more grid step away from the symbol centre.
                if text.position.y < sym_y {
                    text.position.y -= GRID;
                } else {
                    text.position.y += GRID;
                }
            }
        }
    }

    // --- Pass 2: text vs text (across different symbols) ---
    // Collect (symbol_index, text_index, bbox) triples.
    let all_texts: Vec<(usize, usize, BBox)> = schematic
        .symbols
        .iter()
        .enumerate()
        .flat_map(|(si, sym)| {
            sym.texts
                .iter()
                .enumerate()
                .map(move |(ti, t)| (si, ti, estimate_text_bbox(t)))
        })
        .collect();

    // For each pair from *different* symbols, nudge the later one down.
    let mut nudges: Vec<(usize, usize, f64)> = Vec::new();
    for i in 0..all_texts.len() {
        for j in (i + 1)..all_texts.len() {
            let (si, _ti, ref bi) = all_texts[i];
            let (sj, tj, ref bj) = all_texts[j];
            if si == sj {
                continue; // same symbol — handled by vertical layout already
            }
            if bi.overlaps(bj) {
                let text_h = schematic.symbols[sj].texts[tj].height;
                let offset = text_h.max(1.0) + 1.0;
                nudges.push((sj, tj, offset));
            }
        }
    }

    for (si, ti, dy) in nudges {
        schematic.symbols[si].texts[ti].position.y += dy;
    }
}

/// Approximate the axis-aligned bounding box of a [`SchematicText`].
///
/// Uses a simple character-width heuristic (0.6 × height per character) and
/// applies alignment offsets.  For rotated text the box is conservatively
/// expanded.
pub fn estimate_text_bbox(text: &SchematicText) -> BBox {
    let height = if text.height > 0.0 { text.height } else { 2.5 };

    // Estimate display length by replacing template placeholders with
    // representative character counts.
    let display = text
        .value
        .replace("{{NAME}}", "UXX") // 3 chars
        .replace("{{VALUE}}", "100k"); // 4 chars
    let char_count = display.chars().count().max(1);

    let text_width = char_count as f64 * height * 0.6;
    let text_height = height * 1.2;

    // Horizontal extent from anchor.
    let (x0, x1) = match text.align.h {
        HAlign::Left => (text.position.x, text.position.x + text_width),
        HAlign::Center => (
            text.position.x - text_width / 2.0,
            text.position.x + text_width / 2.0,
        ),
        HAlign::Right => (text.position.x - text_width, text.position.x),
    };

    // Vertical extent from anchor.
    let (y0, y1) = match text.align.v {
        VAlign::Top => (text.position.y, text.position.y + text_height),
        VAlign::Center => (
            text.position.y - text_height / 2.0,
            text.position.y + text_height / 2.0,
        ),
        VAlign::Bottom => (text.position.y - text_height, text.position.y),
    };

    // Conservative expansion for rotated text: treat worst-case by adding
    // the height to both axes so the bbox covers any rotation.
    if text.rotation.0.abs() > 1.0 {
        BBox::new(
            x0 - text_height,
            y0 - text_width * 0.5,
            x1 + text_height,
            y1 + text_width * 0.5,
        )
    } else {
        BBox::new(x0, y0, x1, y1)
    }
}
