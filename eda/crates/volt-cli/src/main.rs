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
    /// Run electrical rule check on the circuit
    Erc {
        /// Path to project directory
        #[arg(long, default_value = ".")]
        project: PathBuf,
    },
    /// Manage component instances in the circuit
    Component {
        #[command(subcommand)]
        command: commands::component::ComponentCommands,
    },
    /// Manage nets in the circuit
    Net {
        #[command(subcommand)]
        command: commands::net::NetCommands,
    },
    /// Schematic editing and rendering
    Schematic {
        #[command(subcommand)]
        command: commands::schematic::SchematicCommands,
    },
    /// Board layout editing and rendering
    Board {
        #[command(subcommand)]
        command: commands::board::BoardCommands,
    },
    /// Import external libraries and project data
    Import {
        #[command(subcommand)]
        command: commands::import::ImportCommands,
    },
    /// Search and inspect the embedded project library
    Library {
        #[command(subcommand)]
        command: commands::library::LibraryCommands,
    },
}

fn main() {
    let cli = Cli::parse();

    let result = match cli.command {
        Commands::New { name, output } => commands::new_project(&name, output.as_deref()),
        Commands::Inspect { project } => commands::inspect_project(&project),
        Commands::Erc { project } => commands::erc_command(project),
        Commands::Component { command } => commands::component_command(command),
        Commands::Net { command } => commands::net_command(command),
        Commands::Schematic { command } => commands::schematic_command(command),
        Commands::Board { command } => commands::board_command(command),
        Commands::Import { command } => commands::import_command(command),
        Commands::Library { command } => commands::library_command(command),
    };

    if let Err(e) = result {
        eprintln!("Error: {e}");
        std::process::exit(1);
    }
}
