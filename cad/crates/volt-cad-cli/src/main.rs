//! `volt-cad` — agent-friendly CAD CLI backed by FreeCAD (`freecadcmd`).

mod commands;
mod parse;

use std::process;

use parse::Globals;

fn usage() -> &'static str {
    "Volt CAD — FreeCAD-backed 3D modeling CLI\n\
\n\
USAGE:\n\
    volt-cad [GLOBAL OPTIONS] <COMMAND> ...\n\
\n\
GLOBAL OPTIONS:\n\
    --document <PATH>         Open this `.FCStd` before operations\n\
    --new-doc-name <NAME>     Name for a new document (default: VoltCAD)\n\
    --save-document <PATH>    Save active document after operations\n\
\n\
COMMANDS:\n\
    document new [--name NAME] [--save PATH]\n\
    document open --path PATH [--save-as PATH]\n\
    document save --path PATH\n\
\n\
    solid box --id ID [--origin X,Y,Z] --size X,Y,Z\n\
    solid cylinder --id ID [--base X,Y,Z] [--axis X,Y,Z] --radius R --height H\n\
    solid sphere --id ID [--center X,Y,Z] --radius R\n\
    solid cone --id ID [--base X,Y,Z] [--axis X,Y,Z] --r1 R1 --r2 R2 --height H\n\
\n\
    transform translate --id ID --delta X,Y,Z\n\
    transform rotate --id ID [--origin X,Y,Z] --axis X,Y,Z --angle-deg A\n\
    transform scale --id ID --uniform U [--center X,Y,Z]\n\
\n\
    boolean union --out ID --a ID --b ID\n\
    boolean cut --out ID --base ID --tool ID\n\
    boolean common --out ID --a ID --b ID\n\
\n\
    import step --id ID --path PATH\n\
    import stl --id ID --path PATH\n\
\n\
    export step --id ID --output PATH\n\
    export stl --id ID --output PATH [--linear-deflection MM]\n\
\n\
    mesh from-shape --mesh-id ID --solid-id ID [--linear-deflection MM]\n\
    mesh export-stl --mesh-id ID --output PATH\n\
\n\
    check clearance --a ID --b ID\n\
    check bbox --a ID --b ID\n\
\n\
    run --job PATH.json\n"
}

fn main() {
    let raw: Vec<String> = std::env::args().skip(1).collect();
    if raw.is_empty() || raw.iter().any(|a| a == "-h" || a == "--help") {
        print!("{}", usage());
        return;
    }

    let (globals, rest) = match parse::parse_globals(&raw) {
        Ok(x) => x,
        Err(e) => {
            eprintln!("Error: {e}");
            process::exit(1);
        }
    };

    if rest.is_empty() {
        eprintln!("Error: missing command (try `volt-cad --help`)");
        process::exit(1);
    }

    let result = dispatch(&globals, &rest);
    if let Err(e) = result {
        eprintln!("Error: {e}");
        process::exit(1);
    }
}

fn dispatch(globals: &Globals, rest: &[String]) -> Result<(), String> {
    let cmd = rest[0].as_str();
    let tail = &rest[1..];
    match cmd {
        "document" => commands::document::run(globals, tail),
        "solid" => commands::solid::run(globals, tail),
        "transform" => commands::transform::run(globals, tail),
        "boolean" => commands::boolean::run(globals, tail),
        "import" => commands::import_cmd::run(globals, tail),
        "export" => commands::export_cmd::run(globals, tail),
        "mesh" => commands::mesh::run(globals, tail),
        "check" => commands::check::run(globals, tail),
        "run" => commands::run_job_cli(globals, tail),
        other => Err(format!("unknown command `{other}`")),
    }
}
