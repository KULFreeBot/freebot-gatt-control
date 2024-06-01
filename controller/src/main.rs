use crossterm::{
    execute,
    terminal::{disable_raw_mode, enable_raw_mode, EnterAlternateScreen, LeaveAlternateScreen},
};
use ratatui::{backend::CrosstermBackend, Terminal};
use std::{io, sync::Arc, time::Duration};
use tokio::{sync::Mutex, time::sleep};

mod app;
mod ble;
mod model;

#[tokio::main]
async fn main() -> io::Result<()> {
    // Application state: a vector with FreeBots
    let bots = Arc::new(Mutex::new(Vec::new()));

    // Scan for FreeBots and connect to them in the background
    let ble_discovery_handle = tokio::spawn(ble::scan_unchecked(bots.clone()));

    // Poll bot vector until at least one bot is connected
    while bots.lock().await.is_empty() {
        sleep(Duration::from_millis(100)).await;
    }

    println!("Starting TUI..."); // DEBUG print

    // Initialize terminal for user interface
    let _ = execute!(io::stdout(), EnterAlternateScreen);
    let _ = enable_raw_mode();
    let terminal: app::Tui = Terminal::new(CrosstermBackend::new(io::stdout())).unwrap();

    // Main UI loop
    let app = app::App::new(bots.clone());
    let app_result = app.run(terminal).await;

    // Restore terminal to its original state
    let _ = execute!(io::stdout(), LeaveAlternateScreen);
    let _ = disable_raw_mode();

    println!("Exited TUI"); // DEBUG print

    // Disconnect from all bots
    {
        let locked_bots = bots.lock().await;
        for bot in locked_bots.iter() {
            bot.disconnect().await;
        }
    }

    println!("Disconnected bots"); // DEBUG print

    // Stop discovery task
    ble_discovery_handle.abort();

    // Exit
    app_result
}
