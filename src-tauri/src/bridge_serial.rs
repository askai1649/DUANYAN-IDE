//! OpenSNAR Bridge Serial Backend
//!
//! Wraps the `serialport` crate to provide framed read/write for BridgeFrames.
//!
//! NOTE: `serialport` is temporarily disabled because crate download is blocked
//! in this sandbox/network environment. The protocol layer and Tauri commands
//! compile and can be tested; re-enable the dependency and restore the real
//! implementation once network access to crates.io is restored.

#[allow(unused_imports)]
use std::io::{Read, Write};
#[allow(unused_imports)]
use std::time::Duration;

use crate::bridge_protocol::BridgeLog;
#[cfg(feature = "bridge_serial")]
use crate::bridge_protocol::{BridgeFrame, PKT_COMMAND};

#[cfg(feature = "bridge_serial")]
pub struct BridgeSerial {
    port: Option<Box<dyn serialport::SerialPort>>,
    port_name: String,
    baud: u32,
    read_buf: Vec<u8>,
    seq: u16,
}

#[cfg(not(feature = "bridge_serial"))]
#[allow(dead_code)]
pub struct BridgeSerial {
    port_name: String,
    baud: u32,
    read_buf: Vec<u8>,
    seq: u16,
}

#[allow(dead_code)]
impl BridgeSerial {
    pub fn new_closed() -> Self {
        #[cfg(feature = "bridge_serial")]
        {
            Self {
                port: None,
                port_name: "none".to_string(),
                baud: 115200,
                read_buf: Vec::with_capacity(4096),
                seq: 0,
            }
        }
        #[cfg(not(feature = "bridge_serial"))]
        {
            Self {
                port_name: "none".to_string(),
                baud: 115200,
                read_buf: Vec::with_capacity(4096),
                seq: 0,
            }
        }
    }

    pub fn list_ports() -> Vec<String> {
        #[cfg(feature = "bridge_serial")]
        {
            match serialport::available_ports() {
                Ok(ports) => ports.into_iter().map(|p| p.port_name).collect(),
                Err(_) => Vec::new(),
            }
        }
        #[cfg(not(feature = "bridge_serial"))]
        {
            // Stub: return a few common Windows COM ports so the UI can be tested.
            vec!["COM1".to_string(), "COM3".to_string(), "COM5".to_string()]
        }
    }

    pub fn open(port: &str, baud: u32) -> Result<Self, String> {
        #[cfg(feature = "bridge_serial")]
        {
            let port = serialport::new(port, baud)
                .timeout(Duration::from_millis(100))
                .data_bits(serialport::DataBits::Eight)
                .parity(serialport::Parity::None)
                .stop_bits(serialport::StopBits::One)
                .open()
                .map_err(|e| format!("Failed to open {}: {}", port, e))?;

            Ok(Self {
                port: Some(port),
                port_name: port.to_string(),
                baud,
                read_buf: Vec::with_capacity(4096),
                seq: 0,
            })
        }
        #[cfg(not(feature = "bridge_serial"))]
        {
            Ok(Self {
                port_name: port.to_string(),
                baud,
                read_buf: Vec::with_capacity(4096),
                seq: 0,
            })
        }
    }

    pub fn is_open(&self) -> bool {
        #[cfg(feature = "bridge_serial")]
        {
            self.port.is_some()
        }
        #[cfg(not(feature = "bridge_serial"))]
        {
            self.port_name != "none"
        }
    }

    pub fn close(&mut self) {
        #[cfg(feature = "bridge_serial")]
        {
            self.port.take();
        }
        #[cfg(not(feature = "bridge_serial"))]
        {
            self.port_name = "none".to_string();
        }
    }

    pub fn port_name(&self) -> &str {
        &self.port_name
    }

    pub fn baud(&self) -> u32 {
        self.baud
    }

    /// Non-blocking read of as many frames as currently available.
    pub fn read_frames(&mut self, _max_frames: usize) -> Result<Vec<BridgeLog>, String> {
        #[cfg(feature = "bridge_serial")]
        {
            let mut logs = Vec::new();
            let Some(port) = self.port.as_mut() else {
                return Ok(logs);
            };

            let mut temp = [0u8; 512];
            match port.read(&mut temp) {
                Ok(n) if n > 0 => {
                    self.read_buf.extend_from_slice(&temp[..n]);
                }
                Ok(_) => {}
                Err(ref e) if e.kind() == std::io::ErrorKind::TimedOut => {}
                Err(e) => return Err(format!("Serial read error: {}", e)),
            }

            let now = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap_or_default()
                .as_millis() as u64;

            let mut consumed = 0;
            while logs.len() < _max_frames {
                if self.read_buf.len() < consumed + 8 {
                    break;
                }
                match BridgeFrame::decode(&self.read_buf[consumed..]) {
                    Some((frame, n)) => {
                        consumed += n;
                        logs.push(BridgeLog::from_frame(now, &frame));
                    }
                    None => {
                        consumed += 1;
                    }
                }
            }

            if consumed > 0 {
                self.read_buf.drain(..consumed);
            }

            Ok(logs)
        }
        #[cfg(not(feature = "bridge_serial"))]
        {
            // Stub: return empty.
            Ok(Vec::new())
        }
    }

    /// Send a text command as a BridgeFrame.
    pub fn send_command(&mut self, text: &str) -> Result<(), String> {
        #[cfg(feature = "bridge_serial")]
        {
            let Some(port) = self.port.as_mut() else {
                return Err("Serial port not open".to_string());
            };

            self.seq = self.seq.wrapping_add(1);
            let frame = BridgeFrame::new(PKT_COMMAND, self.seq, text.as_bytes().to_vec());
            let bytes = frame.encode();
            port.write_all(&bytes)
                .and_then(|_| port.flush())
                .map_err(|e| format!("Serial write error: {}", e))?;
            Ok(())
        }
        #[cfg(not(feature = "bridge_serial"))]
        {
            let _ = text;
            Err("Serial port support is disabled in this build. Re-enable the `bridge_serial` feature.".to_string())
        }
    }

    /// Send raw bytes (for transparent UART passthrough).
    pub fn send_raw(&mut self, data: &[u8]) -> Result<(), String> {
        #[cfg(feature = "bridge_serial")]
        {
            let Some(port) = self.port.as_mut() else {
                return Err("Serial port not open".to_string());
            };
            port.write_all(data)
                .and_then(|_| port.flush())
                .map_err(|e| format!("Serial write error: {}", e))?;
            Ok(())
        }
        #[cfg(not(feature = "bridge_serial"))]
        {
            let _ = data;
            Err("Serial port support is disabled in this build. Re-enable the `bridge_serial` feature.".to_string())
        }
    }

    /// Read raw bytes without Bridge-frame decoding. Used by the cluster layer
    /// to parse its own 0x4343 magic frames.
    pub fn read_raw_bytes(&mut self) -> Result<Vec<u8>, String> {
        #[cfg(feature = "bridge_serial")]
        {
            let Some(port) = self.port.as_mut() else {
                return Ok(Vec::new());
            };
            let mut temp = [0u8; 512];
            match port.read(&mut temp) {
                Ok(n) if n > 0 => Ok(temp[..n].to_vec()),
                Ok(_) => Ok(Vec::new()),
                Err(ref e) if e.kind() == std::io::ErrorKind::TimedOut => Ok(Vec::new()),
                Err(e) => Err(format!("Serial read error: {}", e)),
            }
        }
        #[cfg(not(feature = "bridge_serial"))]
        {
            Ok(Vec::new())
        }
    }
}
