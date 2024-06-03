use crossterm::{
    terminal::{disable_raw_mode, enable_raw_mode, EnterAlternateScreen, LeaveAlternateScreen},
    ExecutableCommand,
};
use ratatui::{backend::CrosstermBackend, Terminal};
use std::{io, sync::Arc, time::Duration};
use tokio::{sync::Mutex, time::sleep};
use tracing::info;

mod app;
mod ble;
mod model;

#[tokio::main]
async fn main() -> io::Result<()> {
    // Application state: a vector with FreeBots
    let bots = Arc::new(Mutex::new(Vec::new()));

    // Initialize tracing (logger)
    let log_file = std::fs::File::create("controller.log").unwrap(); // FIXME: write to more sensical place
    let subscriber = tracing_subscriber::fmt()
        .compact()
        .with_target(false)
        .with_level(true)
        .with_file(true)
        .with_line_number(true)
        .with_thread_ids(true)
        .with_ansi(false)
        .with_writer(log_file)
        .finish();
    let _ = tracing::subscriber::set_global_default(subscriber);

    // Scan for FreeBots and connect to them in the background
    info!("Discovering bots...");
    let ble_discovery_handle = tokio::spawn(ble::scan_unchecked(bots.clone()));
    sleep(Duration::from_millis(1000)).await;

    // Initialize terminal for user interface
    info!("Starting TUI...");
    let _ = io::stdout().execute(EnterAlternateScreen);
    let _ = enable_raw_mode();
    let terminal: app::Tui = Terminal::new(CrosstermBackend::new(io::stdout())).unwrap();

    // Main UI loop
    let app = app::App::new(bots.clone());
    let app_result = app.run(terminal).await;

    // Restore terminal to its original state
    let _ = io::stdout().execute(LeaveAlternateScreen);
    let _ = disable_raw_mode();

    info!("Exited TUI");

    // Disconnect from all bots
    {
        let locked_bots = bots.lock().await;
        for bot in locked_bots.iter() {
            bot.disconnect().await;
        }
    }

    info!("Disconnected bots");

    // Stop discovery task
    ble_discovery_handle.abort();

    // Exit
    app_result
}
