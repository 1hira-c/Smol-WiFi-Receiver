// SPDX-License-Identifier: MIT OR Apache-2.0

use std::fmt;

pub const RECORD_SIZE: usize = 16;
pub const REPORT_SIZE: usize = 64;
const METADATA_TYPE: u8 = 250;
const METADATA_MARKER: u8 = 255;
const METADATA_MAGIC: u8 = 0xd6;
const METADATA_VERSION: u8 = 1;
const METADATA_END_MAGIC: u8 = 0x6d;

pub const META_SEQUENCE_VALID: u8 = 1 << 0;
pub const META_RF_V2: u8 = 1 << 1;
pub const META_CRC_VALID: u8 = 1 << 2;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct RecordMetadata {
    pub sequence: u8,
    pub rssi: u8,
    pub flags: u8,
}

impl RecordMetadata {
    pub fn sequence_valid(self) -> bool {
        self.flags & META_SEQUENCE_VALID != 0
    }
    pub fn rf_v2(self) -> bool {
        self.flags & META_RF_V2 != 0
    }
    pub fn crc_valid(self) -> bool {
        self.flags & META_CRC_VALID != 0
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ReportRecord {
    pub data: [u8; RECORD_SIZE],
    pub metadata: Option<RecordMetadata>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DecodeError {
    UnalignedLength(usize),
    InvalidMetadataCount(u8),
}

impl fmt::Display for DecodeError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::UnalignedLength(length) => write!(
                formatter,
                "report length {length} is not divisible by {RECORD_SIZE}"
            ),
            Self::InvalidMetadataCount(count) => {
                write!(formatter, "metadata record count {count} exceeds 3")
            }
        }
    }
}

impl std::error::Error for DecodeError {}

pub fn decode_reports(input: &[u8]) -> Result<Vec<ReportRecord>, DecodeError> {
    if !input.len().is_multiple_of(RECORD_SIZE) {
        return Err(DecodeError::UnalignedLength(input.len()));
    }

    let mut records = Vec::new();
    let mut report_offset = 0;
    while report_offset + REPORT_SIZE <= input.len() {
        let metadata_offset = report_offset + RECORD_SIZE * 3;
        if has_metadata_signature(&input[metadata_offset..metadata_offset + RECORD_SIZE]) {
            let count = input[metadata_offset + 4];
            if count > 3 {
                return Err(DecodeError::InvalidMetadataCount(count));
            }
            // The count covers RF/control data only. Firmware pads the other
            // slots with tracker-address registrations, so all three slots
            // must still be decoded.
            for slot in 0..3 {
                let offset = report_offset + slot * RECORD_SIZE;
                let meta_offset = metadata_offset + 5 + slot * 3;
                records.push(ReportRecord {
                    data: input[offset..offset + RECORD_SIZE]
                        .try_into()
                        .expect("fixed record slice"),
                    metadata: Some(RecordMetadata {
                        sequence: input[meta_offset],
                        rssi: input[meta_offset + 1],
                        flags: input[meta_offset + 2],
                    }),
                });
            }
        } else {
            for slot in 0..4 {
                let offset = report_offset + slot * RECORD_SIZE;
                records.push(ReportRecord {
                    data: input[offset..offset + RECORD_SIZE]
                        .try_into()
                        .expect("fixed record slice"),
                    metadata: None,
                });
            }
        }
        report_offset += REPORT_SIZE;
    }

    while report_offset + RECORD_SIZE <= input.len() {
        records.push(ReportRecord {
            data: input[report_offset..report_offset + RECORD_SIZE]
                .try_into()
                .expect("fixed record slice"),
            metadata: None,
        });
        report_offset += RECORD_SIZE;
    }
    Ok(records)
}

fn has_metadata_signature(data: &[u8]) -> bool {
    data[0] == METADATA_TYPE
        && data[1] == METADATA_MARKER
        && data[2] == METADATA_MAGIC
        && data[3] == METADATA_VERSION
        && data[15] == METADATA_END_MAGIC
}

pub fn registration_address(data: &[u8; RECORD_SIZE]) -> Option<u64> {
    if data[0] != 255 {
        return None;
    }
    let mut address = 0u64;
    for (index, byte) in data[2..8].iter().enumerate() {
        address |= u64::from(*byte) << (index * 8);
    }
    (address != 0).then_some(address)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn decodes_diversity_metadata_report() {
        let mut report = [0u8; REPORT_SIZE];
        report[0] = 2;
        report[16] = 3;
        report[48..53].copy_from_slice(&[250, 255, 0xd6, 1, 2]);
        report[53..59].copy_from_slice(&[7, 44, 7, 8, 50, 3]);
        report[63] = 0x6d;
        let records = decode_reports(&report).unwrap();
        assert_eq!(records.len(), 3);
        assert_eq!(records[0].data[0], 2);
        assert_eq!(records[0].metadata.unwrap().sequence, 7);
        assert_eq!(records[1].metadata.unwrap().rssi, 50);
    }

    #[test]
    fn keeps_registration_records_outside_the_rf_record_count() {
        let mut report = [0u8; REPORT_SIZE];
        report[0] = 2;
        report[16] = 255;
        report[17] = 4;
        report[18..24].copy_from_slice(&[0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12]);
        report[48..53].copy_from_slice(&[250, 255, 0xd6, 1, 1]);
        report[53..56].copy_from_slice(&[7, 44, 7]);
        report[63] = 0x6d;
        let records = decode_reports(&report).unwrap();
        assert_eq!(records.len(), 3);
        assert_eq!(
            registration_address(&records[1].data),
            Some(0x1234_5678_9abc)
        );
    }

    #[test]
    fn extracts_little_endian_registration_address() {
        let mut record = [0u8; RECORD_SIZE];
        record[0] = 255;
        record[2..8].copy_from_slice(&[0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12]);
        assert_eq!(registration_address(&record), Some(0x1234_5678_9abc));
    }

    #[test]
    fn rejects_metadata_with_too_many_rf_records() {
        let mut report = [0u8; REPORT_SIZE];
        report[48..53].copy_from_slice(&[250, 255, 0xd6, 1, 4]);
        report[63] = 0x6d;
        assert_eq!(
            decode_reports(&report),
            Err(DecodeError::InvalidMetadataCount(4))
        );
    }
}
