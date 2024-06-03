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
    layout::{self, Alignment, Constraint, Direction, Layout, Rect},
    style::{Modifier, Style, Stylize},
    widgets::{block::Title, Block, BorderType, Borders, Paragraph, Widget},
    Frame, Terminal,
};
use tokio::time::sleep;
use tracing::{debug, info};

use crate::ble::{DriveCharCmd, FreeBotPeripheral};

pub type Tui = Terminal<CrosstermBackend<Stdout>>;

pub struct App {
    bots: Arc<Mutex<Vec<FreeBotPeripheral>>>,
    shown: Vec<usize>,
    exit: bool,
}

impl App {
    pub fn new(bots: Arc<Mutex<Vec<FreeBotPeripheral>>>) -> Self {
        Self {
            bots,
            exit: false,
            shown: vec![0, 1, 2, 3],
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
            event::KeyCode::Tab => self.cycle_selected().await,
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
        if let Some(bot) = bots.iter().find(|b| b.active) {
            let bot = bot.to_owned(); // Explicit clone such that the send operation can be performed in a background task without locking the mutex
            tokio::spawn(async move {
                bot.drive(cmd).await;
            });
        };
        // TODO: Handle no bot selected
    }

    async fn cycle_selected(&mut self) {
        // Lock bots mutex
        let mut locked_bots = self.bots.lock().await;

        // Get bot count and currently selected bot
        let bot_count = locked_bots.len();
        let bot_index = locked_bots
            .iter()
            .enumerate()
            .find_map(|(i, b)| if b.active { Some(i) } else { None })
            .unwrap_or(0);

        // Unselect all bots /* TODO: may be sufficient to only unselect first selected bot */
        locked_bots.iter_mut().for_each(|b| b.active = false);

        // Select next bot
        let bot_index = (bot_index + 1) % bot_count;
        locked_bots.get_mut(bot_index).unwrap().active = true;

        // DEBUG: Log selected bot
        info!("Selected: {}", locked_bots.get(bot_index).unwrap().address())
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
        let bots = tokio::task::block_in_place(|| self.bots.blocking_lock()); // Lock bots mutex
        let [main, side] =
            Layout::horizontal([Constraint::Percentage(100), Constraint::Min(20)]).areas(area);

        // Render side window with connected bots
        let title = Title::from("Connected").alignment(Alignment::Center);
        let block = Block::default()
            .title(title)
            .borders(Borders::ALL)
            .border_type(BorderType::Rounded);
        let text = bots
            .iter()
            .fold(String::new(), |acc, bot| acc + &bot.address() + "\n");
        Paragraph::new(text)
            .alignment(Alignment::Left)
            .block(block)
            .render(side, buf);

        // Render bot views
        if bots.is_empty() {
            let block = Block::default()
                .borders(Borders::ALL)
                .border_type(BorderType::Rounded);
            let text = "No bots found".to_string();
            Paragraph::new(text)
                .alignment(Alignment::Center)
                .block(block)
                .render(main, buf);
        } else if bots.len() == 1 {
            bots.first().unwrap().render(main, buf);
        } else if bots.len() == 2 {
            let [top, bottom] =
                Layout::vertical([Constraint::Percentage(50), Constraint::Percentage(50)])
                    .areas(main);
            bots.get(0).unwrap().render(top, buf);
            bots.get(1).unwrap().render(bottom, buf);
        } else {
            let [left, right] = Layout::horizontal([Constraint::Fill(1); 2]).areas(main);
            let [q_ii, q_iii] = Layout::vertical([Constraint::Fill(1); 2]).areas(left);
            let [q_i, q_iv] = Layout::vertical([Constraint::Fill(1); 2]).areas(right);
            let quadrants = [q_ii, q_i, q_iii, q_iv];

            for i in self.shown.to_owned() {
                if let Some(bot) = bots.get(i) {
                    bot.render(quadrants[i], buf);
                }
            }
        }
    }
}

impl Widget for &FreeBotPeripheral {
    fn render(self, area: Rect, buf: &mut Buffer) {
        let block = Block::default()
            .borders(Borders::ALL)
            .border_type(BorderType::Rounded);
        let text = self.digital_twin.to_string();
        let style = if self.active {
            Style::default().bold()
        } else {
            Style::default()
        };

        Paragraph::new(text)
            .alignment(Alignment::Center)
            .block(block)
            .style(style)
            .render(area, buf);
    }
}
