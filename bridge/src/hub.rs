// SPDX-License-Identifier: MIT OR Apache-2.0

use crate::dedup::{SequenceDecision, SequenceGate};
use crate::report::{ReportRecord, decode_reports, registration_address};
use std::collections::HashMap;

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct SourceMetrics {
    pub received: u64,
    pub accepted: u64,
    pub duplicates: u64,
    pub stale: u64,
    pub crc_errors: u64,
    pub sequence_missing: u64,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct AcceptedRecord {
    pub tracker_address: u64,
    pub record: ReportRecord,
}

#[derive(Default)]
struct SourceState {
    local_to_address: HashMap<u8, u64>,
}

/// Shared convergence point for all physical receiver sources.
#[derive(Default)]
pub struct ReceiverHub {
    sources: HashMap<String, SourceState>,
    metrics: HashMap<String, SourceMetrics>,
    sequence_gate: SequenceGate,
}

impl ReceiverHub {
    pub fn ingest(
        &mut self,
        source_id: &str,
        input: &[u8],
        now_ms: u64,
    ) -> Result<Vec<AcceptedRecord>, crate::report::DecodeError> {
        let records = decode_reports(input)?;
        let source = self.sources.entry(source_id.to_owned()).or_default();
        let metrics = self.metrics.entry(source_id.to_owned()).or_default();
        let mut accepted = Vec::new();

        for record in records {
            if record.data.iter().all(|byte| *byte == 0) {
                continue;
            }
            let local_id = record.data[1];
            if let Some(address) = registration_address(&record.data) {
                source.local_to_address.insert(local_id, address);
                continue;
            }
            let Some(&tracker_address) = source.local_to_address.get(&local_id) else {
                continue;
            };

            if let Some(metadata) = record.metadata.filter(|metadata| metadata.rf_v2()) {
                metrics.received += 1;
                if !metadata.crc_valid() || !metadata.sequence_valid() {
                    if !metadata.crc_valid() {
                        metrics.crc_errors += 1;
                    }
                    continue;
                }
                let result =
                    self.sequence_gate
                        .evaluate(tracker_address, metadata.sequence, now_ms);
                match result.decision {
                    SequenceDecision::Accepted => {
                        metrics.accepted += 1;
                        metrics.sequence_missing += u64::from(result.missing_packets);
                    }
                    SequenceDecision::Duplicate => {
                        metrics.duplicates += 1;
                        continue;
                    }
                    SequenceDecision::Stale => {
                        metrics.stale += 1;
                        continue;
                    }
                }
            }

            accepted.push(AcceptedRecord {
                tracker_address,
                record,
            });
        }
        Ok(accepted)
    }

    pub fn metrics(&self) -> &HashMap<String, SourceMetrics> {
        &self.metrics
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::report::{META_CRC_VALID, META_RF_V2, META_SEQUENCE_VALID, REPORT_SIZE};

    fn report(local_id: u8, address: u64, packet_type: u8, sequence: u8) -> [u8; REPORT_SIZE] {
        let mut output = [0u8; REPORT_SIZE];
        output[0] = 255;
        output[1] = local_id;
        for index in 0..6 {
            output[2 + index] = (address >> (index * 8)) as u8;
        }
        output[16] = packet_type;
        output[17] = local_id;
        output[48..53].copy_from_slice(&[250, 255, 0xd6, 1, 2]);
        output[53..59].copy_from_slice(&[
            0,
            40,
            META_RF_V2 | META_SEQUENCE_VALID | META_CRC_VALID,
            sequence,
            40,
            META_RF_V2 | META_SEQUENCE_VALID | META_CRC_VALID,
        ]);
        output[63] = 0x6d;
        output
    }

    #[test]
    fn deduplicates_the_same_tracker_across_sources() {
        let mut hub = ReceiverHub::default();
        let first = hub.ingest("hid:a", &report(1, 0x1234, 2, 9), 1000).unwrap();
        let second = hub
            .ingest("wifi:b", &report(7, 0x1234, 2, 9), 1001)
            .unwrap();
        assert_eq!(first.len(), 1);
        assert!(second.is_empty());
        assert_eq!(hub.metrics()["wifi:b"].duplicates, 1);
    }
}
