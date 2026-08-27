// SPDX-License-Identifier: MIT OR Apache-2.0

use crate::gateway::{self, GatewayDatagramGate, GatewayKind};
use crate::hub::ReceiverHub;
use crate::slimevr::SlimeVrOutput;
use hidapi::{HidApi, HidDevice};
use std::collections::{HashMap, HashSet};
use std::ffi::CString;
use std::io;
use std::net::{SocketAddr, UdpSocket};
use std::time::{Duration, Instant};

pub struct GatewayInput {
    socket: UdpSocket,
    gate: GatewayDatagramGate,
    server_sequence: u32,
}

impl GatewayInput {
    pub fn bind(address: SocketAddr) -> io::Result<Self> {
        let socket = UdpSocket::bind(address)?;
        socket.set_nonblocking(true)?;
        Ok(Self {
            socket,
            gate: GatewayDatagramGate::default(),
            server_sequence: 0,
        })
    }

    pub fn local_address(&self) -> io::Result<SocketAddr> {
        self.socket.local_addr()
    }

    pub fn poll(
        &mut self,
        hub: &mut ReceiverHub,
        output: &mut SlimeVrOutput,
        now_ms: u64,
        now: Instant,
    ) -> io::Result<()> {
        let mut input = [0u8; gateway::MAX_DATAGRAM_SIZE];
        loop {
            let (length, source) = match self.socket.recv_from(&mut input) {
                Ok(received) => received,
                Err(error) if error.kind() == io::ErrorKind::WouldBlock => break,
                Err(error) => return Err(error),
            };
            if !gateway::has_magic(&input[..length]) {
                continue;
            }
            let message = match gateway::decode(&input[..length]) {
                Ok(message) => message,
                Err(error) => {
                    eprintln!("gateway {source}: {error}");
                    continue;
                }
            };
            let reply_kind = match message.kind {
                GatewayKind::Discover => Some(GatewayKind::Offer),
                GatewayKind::Hello | GatewayKind::ReportBatch => Some(GatewayKind::Ack),
                GatewayKind::Offer | GatewayKind::Ack => None,
            };
            if let Some(kind) = reply_kind {
                let response = gateway::control_reply(&message, kind, self.server_sequence);
                self.server_sequence = self.server_sequence.wrapping_add(1);
                self.socket.send_to(&response, source)?;
            }
            if message.kind != GatewayKind::ReportBatch || !self.gate.accept(&message) {
                continue;
            }
            let source_id = format!("wifi:{:012X}", message.gateway_id);
            for report in &message.reports {
                match hub.ingest(&source_id, report, now_ms) {
                    Ok(records) => {
                        for record in records {
                            output.process(record, now)?;
                        }
                    }
                    Err(error) => eprintln!("gateway {source_id}: {error}"),
                }
            }
        }
        Ok(())
    }
}

struct OpenHidReceiver {
    source_id: String,
    device: HidDevice,
}

pub struct HidInput {
    api: HidApi,
    products: Vec<(u16, u16)>,
    devices: HashMap<Vec<u8>, OpenHidReceiver>,
    last_refresh: Option<Instant>,
}

impl HidInput {
    pub fn new(products: Vec<(u16, u16)>) -> hidapi::HidResult<Self> {
        Ok(Self {
            api: HidApi::new()?,
            products,
            devices: HashMap::new(),
            last_refresh: None,
        })
    }

    pub fn poll(
        &mut self,
        hub: &mut ReceiverHub,
        output: &mut SlimeVrOutput,
        now_ms: u64,
        now: Instant,
    ) {
        if self
            .last_refresh
            .is_none_or(|last| now.duration_since(last) >= Duration::from_secs(1))
        {
            if let Err(error) = self.refresh() {
                eprintln!("HID enumeration: {error}");
            }
            self.last_refresh = Some(now);
        }

        let mut disconnected = Vec::new();
        for (path, receiver) in &self.devices {
            let mut input = [0u8; 64];
            loop {
                match receiver.device.read_timeout(&mut input, 0) {
                    Ok(0) => break,
                    Ok(length) => match hub.ingest(&receiver.source_id, &input[..length], now_ms) {
                        Ok(records) => {
                            for record in records {
                                if let Err(error) = output.process(record, now) {
                                    eprintln!("{}: SlimeVR output: {error}", receiver.source_id);
                                }
                            }
                        }
                        Err(error) => eprintln!("{}: {error}", receiver.source_id),
                    },
                    Err(error) => {
                        eprintln!("{}: HID read failed: {error}", receiver.source_id);
                        disconnected.push(path.clone());
                        break;
                    }
                }
            }
        }
        for path in disconnected {
            self.devices.remove(&path);
        }
    }

    pub fn device_count(&self) -> usize {
        self.devices.len()
    }

    fn refresh(&mut self) -> hidapi::HidResult<()> {
        self.api.refresh_devices()?;
        let matching = self
            .api
            .device_list()
            .filter(|info| {
                self.products
                    .iter()
                    .any(|product| *product == (info.vendor_id(), info.product_id()))
            })
            .map(|info| {
                (
                    info.path().to_bytes().to_vec(),
                    CString::new(info.path().to_bytes())
                        .expect("HID path contains no interior null"),
                    info.serial_number().map(str::to_owned),
                    info.vendor_id(),
                    info.product_id(),
                )
            })
            .collect::<Vec<_>>();
        let present = matching
            .iter()
            .map(|entry| entry.0.clone())
            .collect::<HashSet<_>>();
        self.devices.retain(|path, _| present.contains(path));

        for (path_key, path, serial, vid, pid) in matching {
            if self.devices.contains_key(&path_key) {
                continue;
            }
            match self.api.open_path(&path) {
                Ok(device) => {
                    let identity = serial.unwrap_or_else(|| "no-serial".to_owned());
                    let path_label = String::from_utf8_lossy(&path_key);
                    let source_id = format!("hid:{vid:04X}:{pid:04X}:{identity}:{path_label}");
                    println!("{source_id}: receiver connected");
                    self.devices
                        .insert(path_key, OpenHidReceiver { source_id, device });
                }
                Err(error) => eprintln!("HID {vid:04X}:{pid:04X}: open failed: {error}"),
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::gateway::{GatewayMessage, encode};

    #[test]
    fn gateway_discovery_gets_an_offer_from_the_bound_socket() {
        let mut input = GatewayInput::bind("127.0.0.1:0".parse().unwrap()).unwrap();
        let client = UdpSocket::bind("127.0.0.1:0").unwrap();
        client
            .set_read_timeout(Some(Duration::from_secs(1)))
            .unwrap();
        let discover = GatewayMessage {
            kind: GatewayKind::Discover,
            flags: 0,
            gateway_id: 0x6655_4433_2211,
            boot_id: 7,
            sequence: 1,
            reports: Vec::new(),
        };
        client
            .send_to(&encode(&discover), input.local_address().unwrap())
            .unwrap();

        let mut hub = ReceiverHub::default();
        let unused_server = UdpSocket::bind("127.0.0.1:0").unwrap();
        let mut output = SlimeVrOutput::new(unused_server.local_addr().unwrap());
        input
            .poll(&mut hub, &mut output, 0, Instant::now())
            .unwrap();

        let mut response = [0u8; gateway::MAX_DATAGRAM_SIZE];
        let length = client.recv(&mut response).unwrap();
        let offer = gateway::decode(&response[..length]).unwrap();
        assert_eq!(offer.kind, GatewayKind::Offer);
        assert_eq!(offer.gateway_id, discover.gateway_id);
        assert_eq!(offer.boot_id, discover.boot_id);
    }
}
