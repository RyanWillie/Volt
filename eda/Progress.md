# Progress

## Status
Completed

## Tasks
- [x] Explore code: gerber.rs, project types, library types, CLI export
- [x] Implement `crates/volt-export/src/excellon.rs`
- [x] Register module in `crates/volt-export/src/lib.rs`
- [x] Wire CLI `export drills` subcommand
- [x] Wire CLI `export gerber` (it was a stub; now calls `gerber::export_all`)
- [x] Add tests (12 unit tests, all passing)
- [x] `cargo build` + `cargo test` pass

## Files Changed
- `crates/volt-export/src/excellon.rs` — new Excellon drill writer + PTH/NPTH/merged export
- `crates/volt-export/src/lib.rs` — register `pub mod excellon;`
- `crates/volt-cli/src/commands/export.rs` — new `Drills` subcommand, wired up `Gerber` subcommand (previously a stub), added `BoardLibrary` impl for `ProjectLibrary`

## Notes
- ExcellonWriter emits metric TZ with integer microns as the coordinate format.
- PTH collects vias + THT footprint-pad holes; coordinates transformed through `gerber::transform_point` to respect device rotation/flip.
- NPTH collects `board.holes`, using the first `path[]` vertex as position.
- Tools are deduplicated within 0.001 mm tolerance and sorted ascending.
- `merge_drills` setting produces an extra merged file alongside the PTH/NPTH pair.
- 12 new tests cover: coordinate formatting, writer header/footer, tool ordering/dedup, via-only PTH, THT pad-hole PTH, NPTH mounting holes, empty board, end-to-end file orchestration (with and without merge).
