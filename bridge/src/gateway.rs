// SPDX-License-Identifier: MIT OR Apache-2.0

use crate::report::REPORT_SIZE;
use std::collections::{HashMap, HashSet};
use std::fmt;

pub const VERSION: u8 = 1;
pub const HEADER_SIZE: usize = 28;
pub const MAX_REPORTS: usize = 8;
pub const MAX_DATAGRAM_SIZE: usize = HEADER_SIZE + REPORT_SIZE * MAX_REPORTS + 4;
const MAGIC: &[u8; 4] = b"SVW1";

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum GatewayKind {
    Discover = 1,
    Offer = 2,
    Hello = 3,
    Ack = 4,
    ReportBatch = 5,
}

impl TryFrom<u8> for GatewayKind {
    type Error = GatewayDecodeError;

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            1 => Ok(Self::Discover),
            2 => Ok(Self::Offer),
            3 => Ok(Self::Hello),
            4 => Ok(Self::Ack),
            5 => Ok(Self::ReportBatch),
            _ => Err(GatewayDecodeError::UnknownKind(value)),
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct GatewayMessage {
    pub kind: GatewayKind,
    pub flags: u8,
    pub gateway_id: u64,
    pub boot_id: u32,
    pub sequence: u32,
    pub reports: Vec<[u8; REPORT_SIZE]>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum GatewayDecodeError {
    TooShort,
    TooLong,
    WrongMagic,
    UnsupportedVersion(u8),
    UnknownKind(u8),
    InvalidReportCount(u8),
    InvalidLength,
    InvalidCrc,
}

impl fmt::Display for GatewayDecodeError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(formatter, "invalid SVW1 datagram: {self:?}")
    }
}

impl std::error::Error for GatewayDecodeError {}

pub fn has_magic(input: &[u8]) -> bool {
    input.starts_with(MAGIC)
}

pub fn decode(input: &[u8]) -> Result<GatewayMessage, GatewayDecodeError> {
    if input.len() < HEADER_SIZE + 4 {
        return Err(GatewayDecodeError::TooShort);
    }
    if input.len() > MAX_DATAGRAM_SIZE {
        return Err(GatewayDecodeError::TooLong);
    }
    if !has_magic(input) {
        return Err(GatewayDecodeError::WrongMagic);
    }
    if input[4] != VERSION {
        return Err(GatewayDecodeError::UnsupportedVersion(input[4]));
    }
    let kind = GatewayKind::try_from(input[5])?;
    let report_count = usize::from(input[7]);
    if report_count > MAX_REPORTS {
        return Err(GatewayDecodeError::InvalidReportCount(input[7]));
    }
    let payload_length = usize::from(read_u16_le(input, 24));
    if read_u16_le(input, 26) != HEADER_SIZE as u16
        || payload_length != report_count * REPORT_SIZE
        || input.len() != HEADER_SIZE + payload_length + 4
        || (kind != GatewayKind::ReportBatch && report_count != 0)
    {
        return Err(GatewayDecodeError::InvalidLength);
    }
    let expected_crc = read_u32_le(input, input.len() - 4);
    if crc32_ieee(&input[..input.len() - 4]) != expected_crc {
        return Err(GatewayDecodeError::InvalidCrc);
    }

    let mut gateway_id = 0u64;
    for (index, byte) in input[8..14].iter().enumerate() {
        gateway_id |= u64::from(*byte) << (index * 8);
    }
    let reports = (0..report_count)
        .map(|index| {
            let offset = HEADER_SIZE + index * REPORT_SIZE;
            input[offset..offset + REPORT_SIZE]
                .try_into()
                .expect("fixed report slice")
        })
        .collect();
    Ok(GatewayMessage {
        kind,
        flags: input[6],
        gateway_id,
        boot_id: read_u32_le(input, 16),
        sequence: read_u32_le(input, 20),
        reports,
    })
}

pub fn encode(message: &GatewayMessage) -> Vec<u8> {
    assert!(message.reports.len() <= MAX_REPORTS);
    assert!(message.kind == GatewayKind::ReportBatch || message.reports.is_empty());
    let payload_length = message.reports.len() * REPORT_SIZE;
    let mut output = vec![0u8; HEADER_SIZE + payload_length + 4];
    output[..4].copy_from_slice(MAGIC);
    output[4] = VERSION;
    output[5] = message.kind as u8;
    output[6] = message.flags;
    output[7] = message.reports.len() as u8;
    for index in 0..6 {
        output[8 + index] = (message.gateway_id >> (index * 8)) as u8;
    }
    write_u32_le(&mut output, 16, message.boot_id);
    write_u32_le(&mut output, 20, message.sequence);
    write_u16_le(&mut output, 24, payload_length as u16);
    write_u16_le(&mut output, 26, HEADER_SIZE as u16);
    for (index, report) in message.reports.iter().enumerate() {
        let offset = HEADER_SIZE + index * REPORT_SIZE;
        output[offset..offset + REPORT_SIZE].copy_from_slice(report);
    }
    let crc_offset = output.len() - 4;
    let crc = crc32_ieee(&output[..crc_offset]);
    write_u32_le(&mut output, crc_offset, crc);
    output
}

pub fn control_reply(request: &GatewayMessage, kind: GatewayKind, sequence: u32) -> Vec<u8> {
    encode(&GatewayMessage {
        kind,
        flags: 0,
        gateway_id: request.gateway_id,
        boot_id: request.boot_id,
        sequence,
        reports: Vec::new(),
    })
}

#[derive(Default)]
pub struct GatewayDatagramGate {
    states: HashMap<u64, GatewayState>,
}

#[derive(Debug)]
struct GatewayState {
    boot_id: u32,
    sequence: u32,
    retired_boot_ids: HashSet<u32>,
}

impl GatewayDatagramGate {
    pub fn accept(&mut self, message: &GatewayMessage) -> bool {
        if message.kind != GatewayKind::ReportBatch {
            return true;
        }
        let Some(previous) = self.states.get_mut(&message.gateway_id) else {
            self.states.insert(
                message.gateway_id,
                GatewayState {
                    boot_id: message.boot_id,
                    sequence: message.sequence,
                    retired_boot_ids: HashSet::new(),
                },
            );
            return true;
        };
        if previous.boot_id != message.boot_id {
            if previous.retired_boot_ids.contains(&message.boot_id) {
                return false;
            }
            previous.retired_boot_ids.insert(previous.boot_id);
            previous.boot_id = message.boot_id;
            previous.sequence = message.sequence;
            return true;
        }
        let delta = message.sequence.wrapping_sub(previous.sequence);
        if delta == 0 || delta >= 0x8000_0000 {
            return false;
        }
        previous.sequence = message.sequence;
        true
    }
}

fn read_u16_le(input: &[u8], offset: usize) -> u16 {
    u16::from_le_bytes(input[offset..offset + 2].try_into().expect("u16 slice"))
}

fn read_u32_le(input: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes(input[offset..offset + 4].try_into().expect("u32 slice"))
}

fn write_u16_le(output: &mut [u8], offset: usize, value: u16) {
    output[offset..offset + 2].copy_from_slice(&value.to_le_bytes());
}

fn write_u32_le(output: &mut [u8], offset: usize, value: u32) {
    output[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
}

fn crc32_ieee(input: &[u8]) -> u32 {
    let mut crc = 0xffff_ffffu32;
    for byte in input {
        crc ^= u32::from(*byte);
        for _ in 0..8 {
            crc = (crc >> 1) ^ (0xedb8_8320u32 & 0u32.wrapping_sub(crc & 1));
        }
    }
    !crc
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn matches_the_shared_hello_golden_vector() {
        let message = GatewayMessage {
            kind: GatewayKind::Hello,
            flags: 0,
            gateway_id: 0x6655_4433_2211,
            boot_id: 0x7856_3412,
            sequence: u32::MAX,
            reports: Vec::new(),
        };
        let expected = [
            0x53, 0x56, 0x57, 0x31, 0x01, 0x03, 0x00, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
            0x00, 0x00, 0x12, 0x34, 0x56, 0x78, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x1c, 0x00,
            0x62, 0xe7, 0x0a, 0xf1,
        ];
        assert_eq!(encode(&message), expected);
        assert_eq!(decode(&expected).unwrap(), message);
    }

    #[test]
    fn rejects_crc_mutation_and_reordered_datagrams() {
        let mut message = GatewayMessage {
            kind: GatewayKind::ReportBatch,
            flags: 0,
            gateway_id: 7,
            boot_id: 1,
            sequence: 10,
            reports: vec![[0u8; REPORT_SIZE]],
        };
        let mut bytes = encode(&message);
        bytes[30] ^= 1;
        assert_eq!(decode(&bytes), Err(GatewayDecodeError::InvalidCrc));

        let mut gate = GatewayDatagramGate::default();
        assert!(gate.accept(&message));
        assert!(!gate.accept(&message));
        message.sequence = 9;
        assert!(!gate.accept(&message));
        message.sequence = 11;
        assert!(gate.accept(&message));
    }
}
