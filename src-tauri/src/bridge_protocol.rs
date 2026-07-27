//! OpenSNAR Bridge Protocol
//!
//! Lightweight binary framing for IDE <-> OpenSNAR OS communication.
//! Header: 8 bytes (big-endian)
//!   magic(2) + pkt_type(1) + seq(2) + len(2) + checksum(1) + payload(len)

#![allow(dead_code)]

use serde::{Deserialize, Serialize};

pub const BRIDGE_MAGIC: u16 = 0x4F53; // "OS"

/// Packet types
pub const PKT_LOG: u8 = 0x01;
pub const PKT_RESULT: u8 = 0x02;
pub const PKT_HW_STATUS: u8 = 0x03;
pub const PKT_EXCEPTION: u8 = 0x04;
pub const PKT_COMMAND: u8 = 0x10;
pub const PKT_HEARTBEAT: u8 = 0x11;

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct BridgeFrame {
    pub magic: u16,
    pub pkt_type: u8,
    pub seq: u16,
    pub payload: Vec<u8>,
}

impl BridgeFrame {
    pub fn new(pkt_type: u8, seq: u16, payload: Vec<u8>) -> Self {
        Self {
            magic: BRIDGE_MAGIC,
            pkt_type,
            seq,
            payload,
        }
    }

    pub fn len(&self) -> u16 {
        self.payload.len() as u16
    }

    pub fn encode(&self) -> Vec<u8> {
        let mut buf = Vec::with_capacity(8 + self.payload.len());
        buf.extend_from_slice(&self.magic.to_be_bytes());
        buf.push(self.pkt_type);
        buf.extend_from_slice(&self.seq.to_be_bytes());
        buf.extend_from_slice(&self.len().to_be_bytes());
        let checksum = self.compute_checksum();
        buf.push(checksum);
        buf.extend_from_slice(&self.payload);
        buf
    }

    /// Decode a frame from buffer. Returns (frame, consumed_bytes) or None if incomplete/invalid.
    pub fn decode(buf: &[u8]) -> Option<(Self, usize)> {
        if buf.len() < 8 {
            return None;
        }
        let magic = u16::from_be_bytes([buf[0], buf[1]]);
        if magic != BRIDGE_MAGIC {
            // Skip first byte and try again (resync)
            return None;
        }
        let pkt_type = buf[2];
        let seq = u16::from_be_bytes([buf[3], buf[4]]);
        let len = u16::from_be_bytes([buf[5], buf[6]]) as usize;
        let checksum = buf[7];
        if buf.len() < 8 + len {
            return None;
        }
        let payload = buf[8..8 + len].to_vec();
        let frame = Self {
            magic,
            pkt_type,
            seq,
            payload,
        };
        if frame.compute_checksum() != checksum {
            return None;
        }
        Some((frame, 8 + len))
    }

    pub fn compute_checksum(&self) -> u8 {
        let mut xor: u8 = 0;
        xor ^= (self.magic >> 8) as u8;
        xor ^= (self.magic & 0xFF) as u8;
        xor ^= self.pkt_type;
        xor ^= (self.seq >> 8) as u8;
        xor ^= (self.seq & 0xFF) as u8;
        let len = self.len();
        xor ^= (len >> 8) as u8;
        xor ^= (len & 0xFF) as u8;
        for b in &self.payload {
            xor ^= b;
        }
        xor
    }
}

/// Log entry exposed to the frontend.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BridgeLog {
    pub timestamp_ms: u64,
    pub pkt_type: u8,
    pub text: String,
    #[serde(default)]
    pub message: Option<BridgeMessage>,
}

impl BridgeLog {
    pub fn from_frame(seq_hint: u64, frame: &BridgeFrame) -> Self {
        let text = String::from_utf8_lossy(&frame.payload).to_string();
        let message = Some(BridgeMessage::from_frame(frame));
        Self {
            timestamp_ms: seq_hint,
            pkt_type: frame.pkt_type,
            text,
            message,
        }
    }
}

/// Parsed OpenSNAR message beyond raw UTF-8 text.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "kind")]
pub enum BridgeMessage {
    Log { text: String },
    Result { values: Vec<i64> },
    HardwareStatus { pin_states: Vec<u8>, adc_values: Vec<u16>, uptime_ms: u64 },
    Exception { code: u32, text: String },
    Command { text: String },
    Heartbeat { uptime_ms: u64 },
    Raw { bytes: Vec<u8> },
}

impl BridgeMessage {
    /// Try to parse a frame payload into a typed message.
    pub fn from_frame(frame: &BridgeFrame) -> Self {
        match frame.pkt_type {
            PKT_LOG => BridgeMessage::Log {
                text: String::from_utf8_lossy(&frame.payload).to_string(),
            },
            PKT_RESULT => {
                let values = parse_i64_list(&frame.payload);
                BridgeMessage::Result { values }
            }
            PKT_HW_STATUS => {
                // Minimal binary format: [pin_count] [pin_states...] [adc_count] [adc_values...] [uptime_be_u64]
                let bytes = &frame.payload;
                if bytes.len() >= 10 {
                    let pin_count = bytes[0] as usize;
                    let mut pos = 1;
                    let pin_states = if bytes.len() >= pos + pin_count {
                        let v = bytes[pos..pos + pin_count].to_vec();
                        pos += pin_count;
                        v
                    } else {
                        Vec::new()
                    };
                    let adc_count = bytes.get(pos).copied().unwrap_or(0) as usize;
                    pos += 1;
                    let mut adc_values = Vec::with_capacity(adc_count);
                    for _ in 0..adc_count {
                        if bytes.len() >= pos + 2 {
                            adc_values.push(u16::from_be_bytes([bytes[pos], bytes[pos + 1]]));
                            pos += 2;
                        }
                    }
                    let uptime_ms = if bytes.len() >= pos + 8 {
                        u64::from_be_bytes([
                            bytes[pos], bytes[pos + 1], bytes[pos + 2], bytes[pos + 3],
                            bytes[pos + 4], bytes[pos + 5], bytes[pos + 6], bytes[pos + 7],
                        ])
                    } else {
                        0
                    };
                    BridgeMessage::HardwareStatus { pin_states, adc_values, uptime_ms }
                } else {
                    BridgeMessage::Raw { bytes: frame.payload.clone() }
                }
            }
            PKT_EXCEPTION => {
                let text = String::from_utf8_lossy(&frame.payload).to_string();
                let code = text.split(':').next().and_then(|s| s.parse().ok()).unwrap_or(0);
                BridgeMessage::Exception { code, text }
            }
            PKT_COMMAND => BridgeMessage::Command {
                text: String::from_utf8_lossy(&frame.payload).to_string(),
            },
            PKT_HEARTBEAT => {
                let uptime_ms = if frame.payload.len() >= 8 {
                    u64::from_be_bytes([
                        frame.payload[0], frame.payload[1], frame.payload[2], frame.payload[3],
                        frame.payload[4], frame.payload[5], frame.payload[6], frame.payload[7],
                    ])
                } else {
                    0
                };
                BridgeMessage::Heartbeat { uptime_ms }
            }
            _ => BridgeMessage::Raw { bytes: frame.payload.clone() },
        }
    }
}

fn parse_i64_list(text: &[u8]) -> Vec<i64> {
    String::from_utf8_lossy(text)
        .split(|c: char| c == ',' || c == '\n' || c == '\r')
        .map(|s| s.trim())
        .filter(|s| !s.is_empty())
        .filter_map(|s| s.parse().ok())
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_roundtrip() {
        let f = BridgeFrame::new(PKT_LOG, 1, b"hello OS".to_vec());
        let enc = f.encode();
        let (dec, n) = BridgeFrame::decode(&enc).unwrap();
        assert_eq!(n, enc.len());
        assert_eq!(f.pkt_type, dec.pkt_type);
        assert_eq!(f.seq, dec.seq);
        assert_eq!(f.payload, dec.payload);
    }
}
