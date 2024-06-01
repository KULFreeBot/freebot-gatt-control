//! Provide a nice TUI to the user

use std::{
    io::{self, Stdout},
    sync::Arc,
    time::Duration,
};

use tokio::sync::Mutex;

use crossterm::event::{self, poll, KeyEvent, KeyEventKind, MouseEventKind};
use ratatui::{
    backend::CrosstermBackend,
    buffer::Buffer,
    layout::{Alignment, Rect},
    widgets::{block::Title, Block, Borders, Paragraph, Widget},
    Frame, Terminal,
};
use tokio::time::sleep;

use crate::ble::{DriveCharCmd, FreeBotPeripheral};

pub type Tui = Terminal<CrosstermBackend<Stdout>>;

pub struct App {
    bots: Arc<Mutex<Vec<FreeBotPeripheral>>>,
    selected: usize,
    exit: bool,
}

impl App {
    pub fn new(bots: Arc<Mutex<Vec<FreeBotPeripheral>>>) -> Self {
        Self {
            bots,
            exit: false,
            selected: 0,
        }
    }

    /// Main application loop.
    ///
    /// This function takes full ownership of [App] and [Tui]
    pub async fn run(mut self, mut term: Tui) -> io::Result<()> {
        while !self.exit {
            term.draw(|frame| self.render_frame(frame))?;
            self.handle_events().await?;
            sleep(Duration::from_millis(16)).await;
        }
        Ok(())
    }

    fn render_frame(&self, frame: &mut Frame) {
        frame.render_widget(self, frame.size());
    }

    async fn handle_events(&mut self) -> io::Result<()> {
        if poll(Duration::ZERO)? {
            match event::read()? {
                event::Event::Key(e) if e.kind == KeyEventKind::Press => {
                    self.handle_key_press_event(e).await
                }
                event::Event::Key(e) if e.kind == KeyEventKind::Release => {
                    self.send_cmd_to_selected(DriveCharCmd::Stop).await
                }
                event::Event::Mouse(e) => { /* TODO: handle mouse events */ }
                _ => {}
            };
        }
        self.update_bots().await; // FIXME: May be to slow for main event loop, offload to other thread?
        Ok(())
    }

    async fn handle_key_press_event(&mut self, key_event: KeyEvent) {
        match key_event.code {
            event::KeyCode::Esc => self.exit = true,
            event::KeyCode::Tab => {/* TODO: make tab cycle trough robots */}
            event::KeyCode::Char('w') => self.send_cmd_to_selected(DriveCharCmd::MvForward).await,
            event::KeyCode::Up => self.send_cmd_to_selected(DriveCharCmd::MvForward).await,
            event::KeyCode::Char('s') => self.send_cmd_to_selected(DriveCharCmd::MvBackward).await,
            event::KeyCode::Down => self.send_cmd_to_selected(DriveCharCmd::MvBackward).await,
            event::KeyCode::Char('a') => self.send_cmd_to_selected(DriveCharCmd::MvLeft).await,
            event::KeyCode::Left => self.send_cmd_to_selected(DriveCharCmd::MvLeft).await,
            event::KeyCode::Char('d') => self.send_cmd_to_selected(DriveCharCmd::MvRight).await,
            event::KeyCode::Right => self.send_cmd_to_selected(DriveCharCmd::MvRight).await,
            event::KeyCode::Char('q') => self.send_cmd_to_selected(DriveCharCmd::RotCcw).await,
            event::KeyCode::Char('e') => self.send_cmd_to_selected(DriveCharCmd::RotCw).await,
            event::KeyCode::Char(' ') => self.send_cmd_to_selected(DriveCharCmd::Stop).await,
            _ => {}
        };
    }

    async fn send_cmd_to_selected(&self, cmd: DriveCharCmd) {
        let bots = self.bots.lock().await;
        if let Some(bot) = bots.get(self.selected) {
            let bot = bot.to_owned(); // Explicit clone such that the send operation can be performed in a background task without locking the mutex
            tokio::spawn(async move {
                bot.drive(cmd).await;
            });
        };
        // TODO: Handle no bot selected
    }

    async fn update_bots(&self) {
        {
            let mut bots = self.bots.lock().await;
            for bot in bots.iter_mut() {
                bot.update().await;
            }
        }
    }
}

impl Widget for &App {
    fn render(self, area: Rect, buf: &mut Buffer) {
        let title = Title::from("FreeBot control").alignment(Alignment::Center);
        let block = Block::default().title(title).borders(Borders::ALL);

        let s = {
            let bots = tokio::task::block_in_place(|| self.bots.blocking_lock());
            if let Some(bot) = bots.get(self.selected) {
                bot.digital_twin.to_string()
            } else {
                "No FreeBot found".to_string()
            }
        };

        Paragraph::new(s).centered().block(block).render(area, buf); // FIXME: What is the overhead, Should this be async?
    }
}
