//! Connect to FreeBots over BLE and send/receive data

use btleplug::{
    api::{
        Central, CentralEvent, Characteristic, Manager as _, Peripheral as _, ScanFilter, Service,
        WriteType,
    },
    platform::{Manager, Peripheral},
};
use futures::StreamExt;
use std::error::Error;
use std::sync::Arc;
use tokio::sync::Mutex;
use tracing::{info, warn};
use uuid::Uuid;

use crate::model::FreeBot;

#[derive(Clone, Copy, Debug)]
enum FreeBotUuid {
    Service,
    DriveChar,
    RpmChar,
    AngleChar,
    VoltageChar,
    LedChar,
}

impl Into<Uuid> for FreeBotUuid {
    fn into(self) -> Uuid {
        match self {
            FreeBotUuid::Service => Uuid::from_u128(0x00000030_0000_1000_8000_00805f9b34fb),
            FreeBotUuid::DriveChar => Uuid::from_u128(0x00000031_0000_1000_8000_00805f9b34fb),
            FreeBotUuid::RpmChar => Uuid::from_u128(0x00000032_0000_1000_8000_00805f9b34fb),
            FreeBotUuid::AngleChar => Uuid::from_u128(0x00000033_0000_1000_8000_00805f9b34fb),
            FreeBotUuid::VoltageChar => Uuid::from_u128(0x00000034_0000_1000_8000_00805f9b34fb),
            FreeBotUuid::LedChar => Uuid::from_u128(0x00000035_0000_1000_8000_00805f9b34fb),
        }
    }
}

#[derive(Debug)]
pub enum DriveCharCmd {
    Stop = 0b0000,
    MvForward = 0b0010,
    MvBackward = 0b0011,
    MvRight = 0b0100,
    MvLeft = 0b0101,
    RotCw = 0b0110,
    RotCcw = 0b0111,
}

#[derive(Debug)]
struct RpmCharData {
    m1: i32,
    m2: i32,
    m3: i32,
    m4: i32,
}

#[derive(Debug)]
struct AngleCharData {
    m1: i32,
    m2: i32,
    m3: i32,
    m4: i32,
}

#[derive(Clone, Debug)]
pub struct FreeBotPeripheral {
    ble_peripheral: Peripheral,
    pub digital_twin: FreeBot,
    pub active: bool,
}

impl FreeBotPeripheral {
    pub async fn disconnect(&self) {
        // TODO: Handle disconnect error
        let _ = self.ble_peripheral.disconnect().await;
    }

    pub fn address(&self) -> String {
        self.ble_peripheral.address().to_string()
    }

    pub async fn drive(&self, cmd: DriveCharCmd) {
        let c = self.characteristic(FreeBotUuid::DriveChar).unwrap();
        // TODO: Handle GATT write error
        let _ = self
            .ble_peripheral
            .write(&c, &[cmd as u8], WriteType::WithoutResponse)
            .await;
    }

    pub async fn update(&mut self) {
        // Update capacitor voltage
        if let Some(v) = self.read_voltage().await {
            self.digital_twin.update_cap_voltage(v.into());
        } else {
            warn!("Could not get Vcap: {}", self.address())
        }

        // Update led status
        if let Some((d15, d16)) = self.read_leds().await {
            self.digital_twin.update_leds(d15, d16);
        } else {
            warn!("Could not get LEDs: {}", self.address())

        }

        // Update motor rpm
        if let Some(rpm) = self.read_rpm().await {
            self.digital_twin
                .update_motor_rpm(rpm.m1, rpm.m2, rpm.m3, rpm.m4)
        } else {
            warn!("Could not get motor RPM: {}", self.address())

        }

        // Update motor angles
        if let Some(angles) = self.read_angle().await {
            self.digital_twin
                .update_motor_angles(angles.m1, angles.m2, angles.m3, angles.m4)
        } else {
            warn!("Could not get motor angles: {}", self.address())
        }
    }

    async fn read_voltage(&self) -> Option<u16> {
        let c = self.characteristic(FreeBotUuid::VoltageChar).unwrap();
        if let Ok(buff) = self.ble_peripheral.read(&c).await {
            if buff.len() == 2 {
                return Some(u16::from_le_bytes(buff.try_into().unwrap()));
            }
        }
        None
    }

    async fn read_rpm(&self) -> Option<RpmCharData> {
        let c = self.characteristic(FreeBotUuid::RpmChar).unwrap();
        if let Ok(buff) = self.ble_peripheral.read(&c).await {
            if buff.len() == 16 {
                return Some(RpmCharData {
                    m1: i32::from_le_bytes(buff[0..4].try_into().unwrap()),
                    m2: i32::from_le_bytes(buff[4..8].try_into().unwrap()),
                    m3: i32::from_le_bytes(buff[8..12].try_into().unwrap()),
                    m4: i32::from_le_bytes(buff[12..16].try_into().unwrap()),
                });
            }
        }
        None
    }

    async fn read_angle(&self) -> Option<AngleCharData> {
        let c = self.characteristic(FreeBotUuid::AngleChar).unwrap();
        if let Ok(buff) = self.ble_peripheral.read(&c).await {
            if buff.len() == 16 {
                return Some(AngleCharData {
                    m1: i32::from_le_bytes(buff[0..4].try_into().unwrap()),
                    m2: i32::from_le_bytes(buff[4..8].try_into().unwrap()),
                    m3: i32::from_le_bytes(buff[8..12].try_into().unwrap()),
                    m4: i32::from_le_bytes(buff[12..16].try_into().unwrap()),
                });
            }
        }
        None
    }

    async fn read_leds(&self) -> Option<(u8, u8)> {
        // FIXME: FreeBot currently has no GATT characteristic for LED status
        if let Some(c) = self.characteristic(FreeBotUuid::LedChar) {
            if let Ok(buff) = self.ble_peripheral.read(&c).await {
                if buff.len() == 1 {
                    return Some(((buff[0] & 0b10) >> 1, buff[0] & 0b01));
                }
            }
        }
        None
    }

    fn characteristic(&self, uuid: FreeBotUuid) -> Option<Characteristic> {
        self.ble_peripheral
            .characteristics()
            .iter()
            .find(|c| c.uuid == uuid.into())
            .map(|e| e.to_owned())
    }
}

/// Populate a vector of FreeBots that are connected over BLE
///
/// Ignores all BLE errors
pub async fn scan_unchecked(bots: Arc<Mutex<Vec<FreeBotPeripheral>>>) {
    let _ = scan(bots).await;
}

async fn scan(bots: Arc<Mutex<Vec<FreeBotPeripheral>>>) -> Result<(), Box<dyn Error>> {
    // Get an adapter (interface) to use for scanning
    let manager = Manager::new().await?;
    let adapter = manager.adapters().await?.first().unwrap().to_owned();

    // Set a scan filter and start scan
    let filter = ScanFilter {
        services: vec![FreeBotUuid::Service.into()],
    };
    adapter.start_scan(filter).await?;

    // Handle scan events
    let mut events = adapter.events().await?;
    while let Some(event) = events.next().await {
        match event {
            CentralEvent::DeviceDiscovered(id) => {
                let p = adapter.peripheral(&id).await?;
                info!("BLE Device Discovered: {}", p.address());

                let s = p.properties().await?.unwrap().services;
                if s.contains(&FreeBotUuid::Service.into()) {
                    p.connect().await?;
                }
            }
            CentralEvent::DeviceConnected(id) => {
                let p = adapter.peripheral(&id).await?;
                info!("BLE Device Connected: {}", p.address());
                p.discover_services().await?;

                if p.services()
                    .iter()
                    .any(|s| s.primary && s.uuid == FreeBotUuid::Service.into())
                {
                    let address = p.address().to_string();
                    bots.lock().await.push(FreeBotPeripheral {
                        ble_peripheral: p,
                        digital_twin: FreeBot::new(address),
                        active: false,
                    });
                } else {
                    p.disconnect().await?;
                }
            }
            CentralEvent::DeviceDisconnected(id) => {
                let p = adapter.peripheral(&id).await?;
                info!("BLE Device Disconnected: {}", p.address());
                bots.lock().await.retain(|p| p.ble_peripheral.id() != id);
            }
            _ => {}
        }
    }
    Ok(())
}
