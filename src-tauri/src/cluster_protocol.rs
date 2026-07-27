//! Dual-FPGA Edge Cluster Protocol (Daisy-Chain Model)
//!
//! Defines the inter-board framing for model-parallel AI inference across a
//! chain of Gowin GW1NSR-4C boards. Only the captain board (shard_id = 0) is
//! connected to the PC via USB-UART. All other boards are pure members
//! connected through the 30-pin ribbon cable (SPI/UART).
//!
//! Frame layout:
//!   magic(2) + msg_type(1) + shard_id(1) + seq(2) + len(2) + checksum(1) + payload(len)
//!
//! shard_id semantics:
//!   0       = captain (connected to PC)
//!   1..N    = member boards in the chain
//!   0xFF    = broadcast

#![allow(dead_code)]

use serde::{Deserialize, Serialize};

pub const CLUSTER_MAGIC: u16 = 0x4343; // "CC" = Cluster Compute

/// Message types
pub const MSG_HANDSHAKE: u8 = 0x01;
pub const MSG_LAYER_ACTIVATION: u8 = 0x10; // forward boundary activation
pub const MSG_LAYER_GRADIENT: u8 = 0x11;    // backward gradient (training)
pub const MSG_INFERENCE_REQ: u8 = 0x20;
pub const MSG_INFERENCE_RSP: u8 = 0x21;
pub const MSG_SYNC: u8 = 0x30;
pub const MSG_ERROR: u8 = 0x40;

/// Transparent forwarding through the captain board to a downstream member.
/// The PC sends this to the captain; the captain re-transmits the embedded
/// ClusterFrame over the SPI/UART chain.
pub const MSG_FORWARD: u8 = 0x50;

/// Chain discovery: captain pings every possible shard_id and counts replies.
pub const MSG_DISCOVER: u8 = 0x51;
pub const MSG_DISCOVER_RSP: u8 = 0x52;

/// Special shard identifiers.
pub const SHARD_CAPTAIN: u8 = 0;
pub const SHARD_BROADCAST: u8 = 0xFF;

/// Identifies which model shard a board owns.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum ShardId {
    Captain,
    Member(u8),
    Broadcast,
}

impl ShardId {
    pub fn as_u8(self) -> u8 {
        match self {
            ShardId::Captain => SHARD_CAPTAIN,
            ShardId::Member(id) => id,
            ShardId::Broadcast => SHARD_BROADCAST,
        }
    }

    pub fn from_u8(v: u8) -> Self {
        match v {
            SHARD_CAPTAIN => ShardId::Captain,
            SHARD_BROADCAST => ShardId::Broadcast,
            n => ShardId::Member(n),
        }
    }

    pub fn is_captain(self) -> bool {
        matches!(self, ShardId::Captain)
    }
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct ClusterFrame {
    pub magic: u16,
    pub msg_type: u8,
    pub shard_id: u8,
    pub seq: u16,
    pub payload: Vec<u8>,
}

impl ClusterFrame {
    pub fn new(msg_type: u8, shard_id: impl IntoShardId, seq: u16, payload: Vec<u8>) -> Self {
        Self {
            magic: CLUSTER_MAGIC,
            msg_type,
            shard_id: shard_id.into_shard_id(),
            seq,
            payload,
        }
    }

    pub fn len(&self) -> u16 {
        self.payload.len() as u16
    }

    pub fn encode(&self) -> Vec<u8> {
        let mut buf = Vec::with_capacity(9 + self.payload.len());
        buf.extend_from_slice(&self.magic.to_be_bytes());
        buf.push(self.msg_type);
        buf.push(self.shard_id);
        buf.extend_from_slice(&self.seq.to_be_bytes());
        buf.extend_from_slice(&self.len().to_be_bytes());
        buf.push(self.checksum());
        buf.extend_from_slice(&self.payload);
        buf
    }

    pub fn decode(buf: &[u8]) -> Option<(Self, usize)> {
        if buf.len() < 9 {
            return None;
        }
        let magic = u16::from_be_bytes([buf[0], buf[1]]);
        if magic != CLUSTER_MAGIC {
            return None;
        }
        let msg_type = buf[2];
        let shard_id = buf[3];
        let seq = u16::from_be_bytes([buf[4], buf[5]]);
        let len = u16::from_be_bytes([buf[6], buf[7]]) as usize;
        let checksum = buf[8];
        if buf.len() < 9 + len {
            return None;
        }
        let payload = buf[9..9 + len].to_vec();
        let frame = Self {
            magic,
            msg_type,
            shard_id,
            seq,
            payload,
        };
        if frame.checksum() != checksum {
            return None;
        }
        Some((frame, 9 + len))
    }

    pub fn checksum(&self) -> u8 {
        let mut xor: u8 = 0;
        xor ^= (self.magic >> 8) as u8;
        xor ^= (self.magic & 0xFF) as u8;
        xor ^= self.msg_type;
        xor ^= self.shard_id;
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

/// Helper trait so `new()` accepts both `ShardId` and raw `u8`.
pub trait IntoShardId {
    fn into_shard_id(self) -> u8;
}

impl IntoShardId for ShardId {
    fn into_shard_id(self) -> u8 {
        self.as_u8()
    }
}

impl IntoShardId for u8 {
    fn into_shard_id(self) -> u8 {
        self
    }
}

/// Typed payload for layer activation exchange.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LayerActivation {
    pub layer_index: u16,
    pub values: Vec<f32>,
}

impl LayerActivation {
    pub fn encode(&self) -> Vec<u8> {
        let mut buf = Vec::with_capacity(2 + self.values.len() * 4);
        buf.extend_from_slice(&self.layer_index.to_be_bytes());
        for v in &self.values {
            buf.extend_from_slice(&v.to_le_bytes());
        }
        buf
    }

    pub fn decode(buf: &[u8]) -> Option<Self> {
        if buf.len() < 2 || (buf.len() - 2) % 4 != 0 {
            return None;
        }
        let layer_index = u16::from_be_bytes([buf[0], buf[1]]);
        let mut values = Vec::with_capacity((buf.len() - 2) / 4);
        for chunk in buf[2..].chunks_exact(4) {
            values.push(f32::from_le_bytes([chunk[0], chunk[1], chunk[2], chunk[3]]));
        }
        Some(Self { layer_index, values })
    }
}

/// Payload used for MSG_FORWARD: the original frame bytes are wrapped as-is.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ForwardEnvelope {
    pub inner: Vec<u8>,
}

impl ForwardEnvelope {
    pub fn wrap(frame: &ClusterFrame) -> Self {
        Self { inner: frame.encode() }
    }

    pub fn unwrap(&self) -> Option<ClusterFrame> {
        ClusterFrame::decode(&self.inner).map(|(f, _)| f)
    }

    pub fn encode(&self) -> Vec<u8> {
        self.inner.clone()
    }

    pub fn decode(buf: &[u8]) -> Option<Self> {
        // Validate that the buffer contains a complete, valid ClusterFrame.
        ClusterFrame::decode(buf)?;
        Some(Self { inner: buf.to_vec() })
    }
}

/// Discovery request/reply payload.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DiscoverRequest {
    pub probe_id: u8,
}

impl DiscoverRequest {
    pub fn encode(&self) -> Vec<u8> {
        vec![self.probe_id]
    }

    pub fn decode(buf: &[u8]) -> Option<Self> {
        if buf.is_empty() {
            return None;
        }
        Some(Self { probe_id: buf[0] })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_cluster_roundtrip() {
        let f = ClusterFrame::new(MSG_HANDSHAKE, ShardId::Captain, 1, vec![0x01, 0x02]);
        let enc = f.encode();
        let (dec, n) = ClusterFrame::decode(&enc).unwrap();
        assert_eq!(n, enc.len());
        assert_eq!(f.msg_type, dec.msg_type);
        assert_eq!(f.shard_id, dec.shard_id);
    }

    #[test]
    fn test_member_shard_id() {
        let f = ClusterFrame::new(MSG_LAYER_ACTIVATION, 3u8, 7, vec![0xAB]);
        assert_eq!(f.shard_id, 3);
    }

    #[test]
    fn test_forward_roundtrip() {
        let inner = ClusterFrame::new(MSG_INFERENCE_REQ, 2u8, 9, vec![0x01, 0x02, 0x03]);
        let env = ForwardEnvelope::wrap(&inner);
        let unwrapped = env.unwrap().unwrap();
        assert_eq!(inner.msg_type, unwrapped.msg_type);
        assert_eq!(inner.shard_id, unwrapped.shard_id);
        assert_eq!(inner.payload, unwrapped.payload);
    }

    #[test]
    fn test_layer_activation_roundtrip() {
        let a = LayerActivation { layer_index: 3, values: vec![1.0, 2.5, -0.5] };
        let enc = a.encode();
        let dec = LayerActivation::decode(&enc).unwrap();
        assert_eq!(a.layer_index, dec.layer_index);
        assert_eq!(a.values, dec.values);
    }
}
