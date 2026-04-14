use clap::{Parser, Subcommand};
use std::path::PathBuf;

mod commands;

#[derive(Parser)]
#[command(name = "volt-eda", version, about = "Volt EDA engine CLI")]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// Create a new Volt EDA project
    New {
        /// Project name
        #[arg(long)]
        name: String,
        /// Output directory (defaults to ./<name>)
        #[arg(long)]
        output: Option<PathBuf>,
    },
    /// Inspect a project and dump summary as JSON
    Inspect {
        /// Path to project directory
        #[arg(long, default_value = ".")]
        project: PathBuf,
    },
}

fn main() {
    let cli = Cli::parse();

    let result = match cli.command {
        Commands::New { name, output } => commands::new_project(&name, output.as_deref()),
        Commands::Inspect { project } => commands::inspect_project(&project),
    };

    if let Err(e) = result {
        eprintln!("Error: {e}");
        std::process::exit(1);
    }
}
