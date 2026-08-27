// SPDX-License-Identifier: MIT OR Apache-2.0

use crate::hub::AcceptedRecord;
use crate::report::{RECORD_SIZE, RecordMetadata};
use std::collections::HashMap;
use std::io;
use std::net::{SocketAddr, UdpSocket};
use std::time::{Duration, Instant};

const PROTOCOL_VERSION: u32 = 22;
const HANDSHAKE_INTERVAL: Duration = Duration::from_secs(1);
const SERVER_TIMEOUT: Duration = Duration::from_secs(3);
const HEARTBEAT_INTERVAL: Duration = Duration::from_secs(1);
const AUX_INTERVAL: Duration = Duration::from_secs(1);
const RSSI_INTERVAL: Duration = Duration::from_millis(250);
const TRACKER_INPUT_TIMEOUT: Duration = Duration::from_secs(3);

const PACKET_HEARTBEAT: u32 = 0;
const PACKET_HANDSHAKE: u32 = 3;
const PACKET_PING_PONG: u32 = 10;
const PACKET_BATTERY_LEVEL: u32 = 12;
const PACKET_TAP: u32 = 13;
const PACKET_SENSOR_INFO: u32 = 15;
const PACKET_ROTATION_DATA: u32 = 17;
const PACKET_SIGNAL_STRENGTH: u32 = 19;
const PACKET_TEMPERATURE: u32 = 20;
const PACKET_ROTATION_AND_ACCELERATION: u32 = 23;

#[derive(Clone, Debug)]
struct TrackerInfo {
    board_type: u32,
    mcu_type: u32,
    imu_type: u8,
    firmware: String,
}

#[derive(Clone, Copy, Debug, Default)]
struct Telemetry {
    battery_level: Option<f32>,
    battery_voltage: Option<f32>,
    temperature: Option<f32>,
    rssi: Option<i32>,
    status: u8,
    button: Option<u8>,
}

pub struct SlimeVrOutput {
    server: SocketAddr,
    trackers: HashMap<u64, TrackerConnection>,
}

impl SlimeVrOutput {
    pub fn new(server: SocketAddr) -> Self {
        Self {
            server,
            trackers: HashMap::new(),
        }
    }

    pub fn process(&mut self, accepted: AcceptedRecord, now: Instant) -> io::Result<()> {
        if !self.trackers.contains_key(&accepted.tracker_address) {
            let connection = TrackerConnection::new(accepted.tracker_address, self.server)?;
            self.trackers.insert(accepted.tracker_address, connection);
        }
        self.trackers
            .get_mut(&accepted.tracker_address)
            .expect("inserted tracker")
            .process(accepted.record.data, accepted.record.metadata, now)
    }

    pub fn tick(&mut self, now: Instant) {
        for (address, tracker) in &mut self.trackers {
            if let Err(error) = tracker.tick(now) {
                eprintln!("output {address:012X}: {error}");
            }
        }
    }

    pub fn tracker_count(&self) -> usize {
        self.trackers.len()
    }
}

struct TrackerConnection {
    address: u64,
    socket: UdpSocket,
    packet_number: u64,
    confirmed: bool,
    info: Option<TrackerInfo>,
    telemetry: Telemetry,
    last_handshake: Option<Instant>,
    last_server_packet: Option<Instant>,
    last_outbound: Option<Instant>,
    last_aux: Option<Instant>,
    last_rssi: Option<Instant>,
    last_input: Option<Instant>,
    input_active: bool,
}

impl TrackerConnection {
    fn new(address: u64, server: SocketAddr) -> io::Result<Self> {
        let bind_address = if server.is_ipv4() {
            "0.0.0.0:0"
        } else {
            "[::]:0"
        };
        let socket = UdpSocket::bind(bind_address)?;
        socket.connect(server)?;
        socket.set_nonblocking(true)?;
        Ok(Self {
            address,
            socket,
            packet_number: 1,
            confirmed: false,
            info: None,
            telemetry: Telemetry {
                status: 1,
                ..Telemetry::default()
            },
            last_handshake: None,
            last_server_packet: None,
            last_outbound: None,
            last_aux: None,
            last_rssi: None,
            last_input: None,
            input_active: false,
        })
    }

    fn process(
        &mut self,
        data: [u8; RECORD_SIZE],
        metadata: Option<RecordMetadata>,
        now: Instant,
    ) -> io::Result<()> {
        let resumed = !self.input_active;
        self.input_active = true;
        self.last_input = Some(now);
        if resumed && data[0] != 3 {
            self.telemetry.status = 1;
        }
        if data[0] == 0 {
            self.info = Some(parse_tracker_info(&data));
        }
        self.update_telemetry(&data, metadata);
        self.ensure_handshake(now)?;
        if !self.confirmed {
            return Ok(());
        }

        if resumed {
            self.send_sensor_info(now)?;
        }

        match data[0] {
            0 => self.send_sensor_info(now)?,
            1 => self.send_packet(
                PACKET_ROTATION_AND_ACCELERATION,
                &full_rotation_acceleration_payload(&data),
                now,
            )?,
            2 | 7 => self.send_packet(
                PACKET_ROTATION_AND_ACCELERATION,
                &reduced_rotation_acceleration_payload(&data),
                now,
            )?,
            3 => self.send_sensor_info(now)?,
            4 => self.send_packet(PACKET_ROTATION_DATA, &full_rotation_payload(&data), now)?,
            6 => self.send_button_if_changed(data[2], now)?,
            _ => {}
        }

        if matches!(data[0], 7) {
            self.send_button_if_changed(data[2], now)?;
        }
        if self
            .last_aux
            .is_none_or(|last| now.duration_since(last) >= AUX_INTERVAL)
            || data[0] == 0
        {
            self.send_aux(now)?;
        }
        if self
            .last_rssi
            .is_none_or(|last| now.duration_since(last) >= RSSI_INTERVAL)
        {
            self.send_rssi(now)?;
        }
        Ok(())
    }

    fn update_telemetry(&mut self, data: &[u8; RECORD_SIZE], metadata: Option<RecordMetadata>) {
        match data[0] {
            0 | 2 => {
                self.telemetry.battery_level = Some(if data[2] == 128 {
                    -1.0
                } else {
                    f32::from(data[2] & 127) / 100.0
                });
                self.telemetry.battery_voltage = Some((f32::from(data[3]) + 245.0) / 100.0);
                self.telemetry.temperature = (data[4] > 0).then(|| f32::from(data[4]) / 2.0 - 39.0);
            }
            3 => self.telemetry.status = map_hid_status(data[2]),
            _ => {}
        }
        let rssi = metadata
            .map(|value| value.rssi)
            .or_else(|| matches!(data[0], 0 | 2 | 3 | 6 | 7).then_some(data[15]));
        if let Some(rssi) = rssi {
            self.telemetry.rssi = Some(-i32::from(rssi));
        }
    }

    fn tick(&mut self, now: Instant) -> io::Result<()> {
        self.poll_server(now)?;
        if self.input_active
            && self
                .last_input
                .is_some_and(|last| now.duration_since(last) >= TRACKER_INPUT_TIMEOUT)
        {
            self.input_active = false;
            self.telemetry.status = 0;
            if self.confirmed {
                self.send_sensor_info(now)?;
            }
            println!("tracker {:012X}: receiver input timed out", self.address);
        }
        if self.confirmed
            && self
                .last_server_packet
                .is_some_and(|last| now.duration_since(last) >= SERVER_TIMEOUT)
        {
            self.confirmed = false;
            self.last_handshake = None;
            println!(
                "tracker {:012X}: SlimeVR Server connection timed out",
                self.address
            );
        }
        self.ensure_handshake(now)?;
        if self.confirmed
            && self.input_active
            && self
                .last_outbound
                .is_none_or(|last| now.duration_since(last) >= HEARTBEAT_INTERVAL)
        {
            self.send_packet(PACKET_HEARTBEAT, &[], now)?;
        }
        Ok(())
    }

    fn poll_server(&mut self, now: Instant) -> io::Result<()> {
        let mut input = [0u8; 256];
        loop {
            match self.socket.recv(&mut input) {
                Ok(length) => {
                    self.last_server_packet = Some(now);
                    if length >= 13 && input[0] == 3 && &input[1..13] == b"Hey OVR =D 5" {
                        if !self.confirmed {
                            self.confirmed = true;
                            println!("tracker {:012X}: connected to SlimeVR Server", self.address);
                            self.send_sensor_info(now)?;
                            self.send_aux(now)?;
                        }
                        continue;
                    }
                    if self.input_active
                        && length >= 16
                        && u32::from_be_bytes(input[..4].try_into().expect("packet id"))
                            == PACKET_PING_PONG
                    {
                        self.send_packet(PACKET_PING_PONG, &input[12..16], now)?;
                    }
                }
                Err(error) if error.kind() == io::ErrorKind::WouldBlock => break,
                Err(error) => return Err(error),
            }
        }
        Ok(())
    }

    fn ensure_handshake(&mut self, now: Instant) -> io::Result<()> {
        if self.confirmed
            || !self.input_active
            || self.info.is_none()
            || self
                .last_handshake
                .is_some_and(|last| now.duration_since(last) < HANDSHAKE_INTERVAL)
        {
            return Ok(());
        }
        let packet = handshake_packet(
            self.address,
            self.info.as_ref().expect("checked tracker info"),
        );
        self.socket.send(&packet)?;
        self.last_handshake = Some(now);
        Ok(())
    }

    fn send_sensor_info(&mut self, now: Instant) -> io::Result<()> {
        let Some(info) = &self.info else {
            return Ok(());
        };
        // Stock SlimeVR UDP has no raw magnetometer-vector packet. Do not
        // advertise a configurable magnetometer that the Bridge cannot drive.
        let payload = [0, self.telemetry.status, info.imu_type];
        self.send_packet(PACKET_SENSOR_INFO, &payload, now)
    }

    fn send_aux(&mut self, now: Instant) -> io::Result<()> {
        if let Some(level) = self.telemetry.battery_level {
            let mut payload = Vec::with_capacity(8);
            payload.extend_from_slice(&self.telemetry.battery_voltage.unwrap_or(0.0).to_be_bytes());
            payload.extend_from_slice(&level.to_be_bytes());
            self.send_packet(PACKET_BATTERY_LEVEL, &payload, now)?;
        }
        if let Some(temperature) = self.telemetry.temperature {
            let mut payload = vec![0];
            payload.extend_from_slice(&temperature.to_be_bytes());
            self.send_packet(PACKET_TEMPERATURE, &payload, now)?;
        }
        self.last_aux = Some(now);
        Ok(())
    }

    fn send_rssi(&mut self, now: Instant) -> io::Result<()> {
        if let Some(rssi) = self.telemetry.rssi {
            let mut payload = vec![0];
            payload.extend_from_slice(&rssi.to_be_bytes());
            self.send_packet(PACKET_SIGNAL_STRENGTH, &payload, now)?;
        }
        self.last_rssi = Some(now);
        Ok(())
    }

    fn send_button_if_changed(&mut self, button: u8, now: Instant) -> io::Result<()> {
        let previous = self.telemetry.button.unwrap_or(0);
        let changed = button & !previous;
        self.telemetry.button = Some(button);
        let action = changed & 0b11;
        if action != 0 {
            self.send_packet(PACKET_TAP, &[0, action], now)?;
        }
        Ok(())
    }

    fn send_packet(&mut self, packet_id: u32, payload: &[u8], now: Instant) -> io::Result<()> {
        let packet = numbered_packet(packet_id, self.packet_number, payload);
        self.packet_number = self.packet_number.wrapping_add(1).max(1);
        self.socket.send(&packet)?;
        self.last_outbound = Some(now);
        Ok(())
    }
}

fn parse_tracker_info(data: &[u8; RECORD_SIZE]) -> TrackerInfo {
    TrackerInfo {
        board_type: u32::from(data[5]),
        mcu_type: u32::from(data[6]),
        imu_type: data[8],
        firmware: format!(
            "Smol Receiver Bridge/{}.{}.{}",
            data[12], data[13], data[14]
        ),
    }
}

fn map_hid_status(status: u8) -> u8 {
    match status {
        0 => 0,
        3 => 2,
        _ => 1,
    }
}

fn handshake_packet(address: u64, info: &TrackerInfo) -> Vec<u8> {
    let firmware = info.firmware.as_bytes();
    let firmware_length = firmware.len().min(253);
    let mut output = numbered_packet_header(PACKET_HANDSHAKE, 0);
    for value in [
        info.board_type,
        u32::from(info.imu_type),
        info.mcu_type,
        0,
        0,
        0,
        PROTOCOL_VERSION,
    ] {
        output.extend_from_slice(&value.to_be_bytes());
    }
    output.push((firmware_length + 1) as u8);
    output.extend_from_slice(&firmware[..firmware_length]);
    output.push(0);
    for shift in [40, 32, 24, 16, 8, 0] {
        output.push((address >> shift) as u8);
    }
    output
}

fn numbered_packet(packet_id: u32, packet_number: u64, payload: &[u8]) -> Vec<u8> {
    let mut output = numbered_packet_header(packet_id, packet_number);
    output.extend_from_slice(payload);
    output
}

fn numbered_packet_header(packet_id: u32, packet_number: u64) -> Vec<u8> {
    let mut output = Vec::with_capacity(12);
    output.extend_from_slice(&packet_id.to_be_bytes());
    output.extend_from_slice(&packet_number.to_be_bytes());
    output
}

fn full_rotation_acceleration_payload(data: &[u8; RECORD_SIZE]) -> Vec<u8> {
    let mut output = Vec::with_capacity(15);
    output.push(0);
    for offset in (2..16).step_by(2) {
        let value = i16::from_le_bytes([data[offset], data[offset + 1]]);
        output.extend_from_slice(&value.to_be_bytes());
    }
    output
}

fn reduced_rotation_acceleration_payload(data: &[u8; RECORD_SIZE]) -> Vec<u8> {
    let q_buffer = u32::from_le_bytes(data[5..9].try_into().expect("reduced quaternion"));
    let mut vector = [
        (q_buffer & 1023) as f32 / 1024.0,
        ((q_buffer >> 10) & 2047) as f32 / 2048.0,
        ((q_buffer >> 21) & 2047) as f32 / 2048.0,
    ];
    for value in &mut vector {
        *value = *value * 2.0 - 1.0;
    }
    let d = vector.iter().map(|value| value * value).sum::<f32>();
    let inv_sqrt_d = 1.0 / (d + 1e-6).sqrt();
    let angle = std::f32::consts::FRAC_PI_2 * d * inv_sqrt_d;
    let k = angle.sin() * inv_sqrt_d;
    let quaternion = [k * vector[0], k * vector[1], k * vector[2], angle.cos()];

    let mut output = Vec::with_capacity(15);
    output.push(0);
    for value in quaternion {
        let q15 = (value * 32768.0).round().clamp(-32768.0, 32767.0) as i16;
        output.extend_from_slice(&q15.to_be_bytes());
    }
    for offset in (9..15).step_by(2) {
        let value = i16::from_le_bytes([data[offset], data[offset + 1]]);
        output.extend_from_slice(&value.to_be_bytes());
    }
    output
}

fn full_rotation_payload(data: &[u8; RECORD_SIZE]) -> Vec<u8> {
    let mut output = Vec::with_capacity(19);
    output.extend_from_slice(&[0, 1]);
    for offset in (2..10).step_by(2) {
        let value = f32::from(i16::from_le_bytes([data[offset], data[offset + 1]])) / 32768.0;
        output.extend_from_slice(&value.to_be_bytes());
    }
    output.push(0);
    output
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::report::ReportRecord;
    use std::net::UdpSocket;

    #[test]
    fn handshake_uses_tracker_address_as_mac() {
        let info = TrackerInfo {
            board_type: 24,
            mcu_type: 10,
            imu_type: 16,
            firmware: "bridge".to_owned(),
        };
        let packet = handshake_packet(0x1234_5678_9abc, &info);
        assert_eq!(&packet[..12], &[0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0]);
        assert_eq!(
            &packet[packet.len() - 6..],
            &[0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc]
        );
    }

    #[test]
    fn full_precision_payload_changes_hid_little_endian_to_udp_big_endian() {
        let mut record = [0u8; RECORD_SIZE];
        record[0] = 1;
        for index in 0..7 {
            let value = (index as i16 + 1) * 0x101;
            record[2 + index * 2..4 + index * 2].copy_from_slice(&value.to_le_bytes());
        }
        let payload = full_rotation_acceleration_payload(&record);
        assert_eq!(payload[0], 0);
        for index in 0..7 {
            assert_eq!(
                &payload[1 + index * 2..3 + index * 2],
                &((index as i16 + 1) * 0x101).to_be_bytes()
            );
        }
    }

    #[test]
    fn reduced_zero_vector_decodes_to_identity_quaternion() {
        let mut record = [0u8; RECORD_SIZE];
        record[0] = 2;
        let center = 512u32 | (1024u32 << 10) | (1024u32 << 21);
        record[5..9].copy_from_slice(&center.to_le_bytes());
        let payload = reduced_rotation_acceleration_payload(&record);
        let q = (0..4)
            .map(|index| {
                i16::from_be_bytes(payload[1 + index * 2..3 + index * 2].try_into().unwrap())
            })
            .collect::<Vec<_>>();
        assert!(q[0].abs() <= 1 && q[1].abs() <= 1 && q[2].abs() <= 1);
        assert_eq!(q[3], 32767);
    }

    #[test]
    fn udp_session_handshakes_then_forwards_sensor_and_rotation() {
        let server = UdpSocket::bind("127.0.0.1:0").unwrap();
        server
            .set_read_timeout(Some(Duration::from_secs(1)))
            .unwrap();
        let mut output = SlimeVrOutput::new(server.local_addr().unwrap());
        let now = Instant::now();
        let address = 0x1234_5678_9abc;
        let mut info = [0u8; RECORD_SIZE];
        info[0] = 0;
        info[1] = 7;
        info[5] = 24;
        info[6] = 10;
        info[8] = 16;
        info[12..15].copy_from_slice(&[1, 2, 3]);
        output
            .process(
                AcceptedRecord {
                    tracker_address: address,
                    record: ReportRecord {
                        data: info,
                        metadata: None,
                    },
                },
                now,
            )
            .unwrap();

        let mut buffer = [0u8; 256];
        let (length, tracker_socket) = server.recv_from(&mut buffer).unwrap();
        assert_eq!(
            u32::from_be_bytes(buffer[..4].try_into().unwrap()),
            PACKET_HANDSHAKE
        );
        assert_eq!(
            &buffer[length - 6..length],
            &[0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc]
        );

        let mut response = [0u8; 64];
        response[0] = 3;
        response[1..13].copy_from_slice(b"Hey OVR =D 5");
        server.send_to(&response, tracker_socket).unwrap();
        output.tick(now + Duration::from_millis(1));

        let mut saw_sensor_info = false;
        for _ in 0..4 {
            let (length, _) = server.recv_from(&mut buffer).unwrap();
            if length >= 12
                && u32::from_be_bytes(buffer[..4].try_into().unwrap()) == PACKET_SENSOR_INFO
            {
                saw_sensor_info = true;
                break;
            }
        }
        assert!(saw_sensor_info);

        let mut rotation = [0u8; RECORD_SIZE];
        rotation[0] = 1;
        rotation[1] = 7;
        rotation[8..10].copy_from_slice(&i16::MAX.to_le_bytes());
        output
            .process(
                AcceptedRecord {
                    tracker_address: address,
                    record: ReportRecord {
                        data: rotation,
                        metadata: None,
                    },
                },
                now + Duration::from_millis(2),
            )
            .unwrap();
        loop {
            let (length, _) = server.recv_from(&mut buffer).unwrap();
            if length >= 12
                && u32::from_be_bytes(buffer[..4].try_into().unwrap())
                    == PACKET_ROTATION_AND_ACCELERATION
            {
                break;
            }
        }
    }
}
