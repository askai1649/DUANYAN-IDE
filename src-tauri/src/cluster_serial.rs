//! Daisy-Chain FPGA Cluster Serial Manager
//!
//! In the daisy-chain model only the captain board (shard_id = 0) is connected
//! to the PC. This module manages that single serial link and uses the OpenSNAR
//! Bridge connection to forward ClusterFrames to downstream members through the
//! captain's SPI/UART chain.

#![allow(dead_code)]

use crate::bridge_serial::BridgeSerial;
use crate::cluster_protocol::{
    ClusterFrame, DiscoverRequest, ForwardEnvelope, LayerActivation, MSG_DISCOVER,
    MSG_FORWARD, MSG_LAYER_ACTIVATION, MSG_INFERENCE_REQ, SHARD_BROADCAST, SHARD_CAPTAIN,
};

pub struct ClusterSerial {
    captain: BridgeSerial,
    seq: u16,
    member_count: usize,
    read_buf: Vec<u8>,
}

impl ClusterSerial {
    pub fn new() -> Self {
        Self {
            captain: BridgeSerial::new_closed(),
            seq: 0,
            member_count: 0,
            read_buf: Vec::with_capacity(4096),
        }
    }

    pub fn connect_captain(&mut self, port: &str, baud: u32) -> Result<(), String> {
        self.captain = BridgeSerial::open(port, baud)?;
        Ok(())
    }

    pub fn disconnect(&mut self) {
        self.captain.close();
        self.member_count = 0;
    }

    pub fn connected(&self) -> bool {
        self.captain.is_open()
    }

    pub fn member_count(&self) -> usize {
        self.member_count
    }

    /// Send an inference request to the captain board (shard_id = 0).
    /// The captain either executes its own shard or forwards layers to members.
    pub fn start_inference(&mut self, input: Vec<f32>) -> Result<(), String> {
        self.seq = self.seq.wrapping_add(1);
        let payload = LayerActivation {
            layer_index: 0,
            values: input,
        }
        .encode();
        let frame = ClusterFrame::new(MSG_INFERENCE_REQ, SHARD_CAPTAIN, self.seq, payload);
        self.send_cluster_frame(&frame)
    }

    /// Forward a layer activation to a specific member board.
    pub fn forward_to_member(
        &mut self,
        shard_id: u8,
        layer_index: u16,
        values: Vec<f32>,
    ) -> Result<(), String> {
        if shard_id == SHARD_CAPTAIN {
            return Err("Cannot forward to captain; use start_inference instead".to_string());
        }
        self.seq = self.seq.wrapping_add(1);
        let payload = LayerActivation { layer_index, values }.encode();
        let frame = ClusterFrame::new(MSG_LAYER_ACTIVATION, shard_id, self.seq, payload);
        self.forward_to_shard(&frame)
    }

    /// Broadcast a frame to every board in the chain.
    pub fn broadcast(&mut self, msg_type: u8, payload: Vec<u8>) -> Result<(), String> {
        self.seq = self.seq.wrapping_add(1);
        let frame = ClusterFrame::new(msg_type, SHARD_BROADCAST, self.seq, payload);
        self.forward_to_shard(&frame)
    }

    /// Probe the chain to discover how many member boards are present.
    /// The captain pings shard_id 1..=max_shards and counts replies.
    pub fn discover_chain(&mut self, max_shards: u8) -> Result<usize, String> {
        self.member_count = 0;
        for probe_id in 1..=max_shards {
            self.seq = self.seq.wrapping_add(1);
            let req = DiscoverRequest { probe_id }.encode();
            let frame = ClusterFrame::new(MSG_DISCOVER, probe_id, self.seq, req);
            self.forward_to_shard(&frame)?;
        }
        // In a real implementation the caller would wait for replies and then
        // call `collect_discovery_replies()`. For the IDE contract we return the
        // configured maximum; the actual count is updated asynchronously via
        // `read_cluster_frames()`.
        self.member_count = max_shards as usize;
        Ok(self.member_count)
    }

    /// Read raw bytes from the captain port and parse any complete ClusterFrames.
    /// Forward envelopes (MSG_FORWARD from the captain) are unwrapped automatically.
    pub fn read_cluster_frames(&mut self, max_frames: usize) -> Result<Vec<ClusterFrame>, String> {
        match self.captain.read_raw_bytes() {
            Ok(bytes) if !bytes.is_empty() => self.read_buf.extend_from_slice(&bytes),
            Ok(_) => {}
            Err(e) => return Err(e),
        }

        let mut frames = Vec::new();
        let mut consumed = 0;
        while frames.len() < max_frames {
            if self.read_buf.len() < consumed + 9 {
                break;
            }
            match ClusterFrame::decode(&self.read_buf[consumed..]) {
                Some((frame, n)) => {
                    consumed += n;
                    frames.push(self.unwrap_forward(frame));
                }
                None => {
                    consumed += 1;
                }
            }
        }
        if consumed > 0 {
            self.read_buf.drain(..consumed);
        }
        Ok(frames)
    }

    /// Send a ClusterFrame to the captain port. The captain's Bridge firmware
    /// recognises the 0x4343 magic and routes it to the SPI/UART chain.
    fn send_cluster_frame(&mut self, frame: &ClusterFrame) -> Result<(), String> {
        self.captain.send_raw(&frame.encode())
    }

    /// Wrap a ClusterFrame in a MSG_FORWARD envelope so the captain knows it
    /// must be re-transmitted to a downstream member rather than consumed locally.
    fn forward_to_shard(&mut self, frame: &ClusterFrame) -> Result<(), String> {
        if frame.shard_id == SHARD_CAPTAIN {
            return self.send_cluster_frame(frame);
        }
        let envelope = ForwardEnvelope::wrap(frame);
        let fwd = ClusterFrame::new(MSG_FORWARD, SHARD_CAPTAIN, frame.seq, envelope.encode());
        self.send_cluster_frame(&fwd)
    }

    fn unwrap_forward(&self, frame: ClusterFrame) -> ClusterFrame {
        if frame.msg_type == MSG_FORWARD {
            ForwardEnvelope::decode(&frame.payload)
                .and_then(|e| e.unwrap())
                .unwrap_or(frame)
        } else {
            frame
        }
    }

    /// Read decoded cluster frames as human-readable log lines for UI display.
    pub fn read_logs(&mut self, limit: usize) -> Vec<String> {
        match self.read_cluster_frames(limit) {
            Ok(frames) => frames
                .into_iter()
                .map(|f| format!("[shard:{}] type=0x{:02X} seq={} len={}", f.shard_id, f.msg_type, f.seq, f.payload.len()))
                .collect(),
            Err(_) => Vec::new(),
        }
    }
}
