// SPDX-License-Identifier: MIT OR Apache-2.0

use std::collections::HashMap;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SequenceDecision {
    Accepted,
    Duplicate,
    Stale,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SequenceResult {
    pub decision: SequenceDecision,
    pub missing_packets: u8,
}

#[derive(Clone, Copy, Debug)]
struct SequenceState {
    sequence: u8,
    received_at_ms: u64,
}

/// First-arrival RF sequence gate keyed by the tracker's 48-bit address.
pub struct SequenceGate {
    reset_after_ms: u64,
    states: HashMap<u64, SequenceState>,
}

impl Default for SequenceGate {
    fn default() -> Self {
        Self::new(250)
    }
}

impl SequenceGate {
    pub fn new(reset_after_ms: u64) -> Self {
        Self {
            reset_after_ms,
            states: HashMap::new(),
        }
    }

    pub fn evaluate(&mut self, tracker_address: u64, sequence: u8, now_ms: u64) -> SequenceResult {
        let Some(previous) = self.states.get(&tracker_address).copied() else {
            self.states.insert(
                tracker_address,
                SequenceState {
                    sequence,
                    received_at_ms: now_ms,
                },
            );
            return SequenceResult {
                decision: SequenceDecision::Accepted,
                missing_packets: 0,
            };
        };

        if now_ms.saturating_sub(previous.received_at_ms) >= self.reset_after_ms {
            self.states.insert(
                tracker_address,
                SequenceState {
                    sequence,
                    received_at_ms: now_ms,
                },
            );
            return SequenceResult {
                decision: SequenceDecision::Accepted,
                missing_packets: 0,
            };
        }

        let delta = sequence.wrapping_sub(previous.sequence);
        match delta {
            0 => {
                self.states.insert(
                    tracker_address,
                    SequenceState {
                        sequence: previous.sequence,
                        received_at_ms: now_ms,
                    },
                );
                SequenceResult {
                    decision: SequenceDecision::Duplicate,
                    missing_packets: 0,
                }
            }
            1..=127 => {
                self.states.insert(
                    tracker_address,
                    SequenceState {
                        sequence,
                        received_at_ms: now_ms,
                    },
                );
                SequenceResult {
                    decision: SequenceDecision::Accepted,
                    missing_packets: delta - 1,
                }
            }
            _ => {
                self.states.insert(
                    tracker_address,
                    SequenceState {
                        sequence: previous.sequence,
                        received_at_ms: now_ms,
                    },
                );
                SequenceResult {
                    decision: SequenceDecision::Stale,
                    missing_packets: 0,
                }
            }
        }
    }

    pub fn clear(&mut self, tracker_address: u64) {
        self.states.remove(&tracker_address);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const TRACKER: u64 = 0x1234_5678_9abc;

    #[test]
    fn first_copy_wins_and_wraps() {
        let mut gate = SequenceGate::default();
        assert_eq!(
            gate.evaluate(TRACKER, 255, 1000).decision,
            SequenceDecision::Accepted
        );
        assert_eq!(
            gate.evaluate(TRACKER, 255, 1001).decision,
            SequenceDecision::Duplicate
        );
        assert_eq!(
            gate.evaluate(TRACKER, 0, 1002).decision,
            SequenceDecision::Accepted
        );
    }

    #[test]
    fn reports_gaps_and_rejects_stale_packets() {
        let mut gate = SequenceGate::default();
        gate.evaluate(TRACKER, 10, 1000);
        assert_eq!(
            gate.evaluate(TRACKER, 13, 1001),
            SequenceResult {
                decision: SequenceDecision::Accepted,
                missing_packets: 2
            }
        );
        assert_eq!(
            gate.evaluate(TRACKER, 12, 1002).decision,
            SequenceDecision::Stale
        );
    }

    #[test]
    fn duplicate_traffic_does_not_reset_sequence_state() {
        let mut gate = SequenceGate::new(250);
        gate.evaluate(TRACKER, 42, 1000);
        assert_eq!(
            gate.evaluate(TRACKER, 42, 1249).decision,
            SequenceDecision::Duplicate
        );
        assert_eq!(
            gate.evaluate(TRACKER, 42, 1251).decision,
            SequenceDecision::Duplicate
        );
        assert_eq!(
            gate.evaluate(TRACKER, 5, 1501).decision,
            SequenceDecision::Accepted
        );
    }
}
