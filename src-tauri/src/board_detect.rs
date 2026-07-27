//! Board Detection Module for SNAR IDE
//!
//! Phase G: USB device scanning, board info, and pin definitions.

use serde::Serialize;

/// Serializable board info for the frontend
#[derive(Debug, Clone, Serialize)]
pub struct BoardInfoDto {
    pub name: String,
    pub description: String,
    pub mcu: String,
    pub frequency_mhz: u32,
    pub ram_kb: u32,
    pub flash_kb: u32,
    pub gpio_count: u32,
    pub uart_count: u32,
    pub adc_channels: u32,
    pub pwm_channels: u32,
    pub spi_count: u32,
    pub i2c_count: u32,
    pub usb_pid: Option<u16>,
    pub usb_vid: Option<u16>,
}

impl From<&snarjs::board::BoardInfo> for BoardInfoDto {
    fn from(b: &snarjs::board::BoardInfo) -> Self {
        Self {
            name: b.name.clone(),
            description: b.description.clone(),
            mcu: b.mcu.clone(),
            frequency_mhz: b.frequency_mhz,
            ram_kb: b.ram_kb,
            flash_kb: b.flash_kb,
            gpio_count: b.gpio_count,
            uart_count: b.uart_count,
            adc_channels: b.adc_channels,
            pwm_channels: b.pwm_channels,
            spi_count: b.spi_count,
            i2c_count: b.i2c_count,
            usb_pid: b.usb_pid,
            usb_vid: b.usb_vid,
        }
    }
}

/// A board detected on a USB serial port
#[derive(Debug, Clone, Serialize)]
pub struct DetectedBoard {
    pub port: String,
    pub description: String,
    pub board: Option<BoardInfoDto>,
    pub vid: Option<u16>,
    pub pid: Option<u16>,
}

/// Pin function type for color coding
#[derive(Debug, Clone, Serialize)]
pub enum PinFunction {
    GPIO,
    UART,
    SPI,
    I2C,
    ADC,
    PWM,
    Power,
    Ground,
    Special,
}

/// A single pin definition
#[derive(Debug, Clone, Serialize)]
pub struct PinInfo {
    pub number: u32,
    pub label: String,
    pub function: PinFunction,
    pub description: String,
    pub side: String, // "left" or "right"
}

/// Get all known boards as DTOs
pub fn get_all_boards() -> Vec<BoardInfoDto> {
    snarjs::board::get_known_boards()
        .iter()
        .map(BoardInfoDto::from)
        .collect()
}

/// Scan USB ports and try to identify connected boards
pub fn scan_boards() -> Vec<DetectedBoard> {
    let known = snarjs::board::get_known_boards();
    let ports = snarjs::board::scan_serial_ports();

    ports
        .into_iter()
        .map(|p| {
            // Try to match by port name or known VID/PID
            // In stub mode, we can't get real VID/PID, so just list ports
            DetectedBoard {
                port: p.name.clone(),
                description: p.description.clone(),
                board: None,
                vid: None,
                pid: None,
            }
        })
        .collect::<Vec<_>>()
}

/// Get pin definitions for a specific board
pub fn get_board_pins(board_name: &str) -> Vec<PinInfo> {
    let name_lower = board_name.to_lowercase();

    // ESP32 pin definitions
    if name_lower.contains("esp32-s3") || name_lower.contains("esp32") {
        return esp32_pins(if name_lower.contains("s3") { "s3" } else { "wroom" });
    }
    if name_lower.contains("arduino uno") || name_lower.contains("atmega328") {
        return arduino_uno_pins();
    }
    if name_lower.contains("arduino nano") {
        return arduino_uno_pins(); // Same as Uno
    }
    if name_lower.contains("arduino mega") {
        return arduino_mega_pins();
    }
    if name_lower.contains("stm32") || name_lower.contains("bluepill") {
        return stm32_pins();
    }
    if name_lower.contains("pico") || name_lower.contains("rp2040") {
        return pico_pins();
    }
    if name_lower.contains("counpre") {
        return counpre64_pins();
    }

    // Default: generic GPIO pins
    generic_pins()
}

// ========== Pin Definitions ==========

fn esp32_pins(variant: &str) -> Vec<PinInfo> {
    let mut pins = Vec::new();
    let gpio_count = if variant == "s3" { 45 } else { 34 };

    // Power pins
    pins.push(PinInfo { number: 0, label: "3V3".into(), function: PinFunction::Power, description: "3.3V Power".into(), side: "left".into() });
    pins.push(PinInfo { number: 1, label: "GND".into(), function: PinFunction::Ground, description: "Ground".into(), side: "left".into() });
    pins.push(PinInfo { number: 2, label: "5V".into(), function: PinFunction::Power, description: "5V Input".into(), side: "left".into() });

    // UART
    pins.push(PinInfo { number: 3, label: "GPIO1/TX0".into(), function: PinFunction::UART, description: "UART0 TX".into(), side: "left".into() });
    pins.push(PinInfo { number: 4, label: "GPIO3/RX0".into(), function: PinFunction::UART, description: "UART0 RX".into(), side: "left".into() });
    pins.push(PinInfo { number: 5, label: "GPIO17/TX2".into(), function: PinFunction::UART, description: "UART2 TX".into(), side: "left".into() });
    pins.push(PinInfo { number: 6, label: "GPIO16/RX2".into(), function: PinFunction::UART, description: "UART2 RX".into(), side: "left".into() });

    // SPI
    pins.push(PinInfo { number: 7, label: "GPIO5/SS".into(), function: PinFunction::SPI, description: "SPI SS".into(), side: "right".into() });
    pins.push(PinInfo { number: 8, label: "GPIO18/SCK".into(), function: PinFunction::SPI, description: "SPI SCK".into(), side: "right".into() });
    pins.push(PinInfo { number: 9, label: "GPIO19/MISO".into(), function: PinFunction::SPI, description: "SPI MISO".into(), side: "right".into() });
    pins.push(PinInfo { number: 10, label: "GPIO23/MOSI".into(), function: PinFunction::SPI, description: "SPI MOSI".into(), side: "right".into() });

    // I2C
    pins.push(PinInfo { number: 11, label: "GPIO21/SDA".into(), function: PinFunction::I2C, description: "I2C SDA".into(), side: "right".into() });
    pins.push(PinInfo { number: 12, label: "GPIO22/SCL".into(), function: PinFunction::I2C, description: "I2C SCL".into(), side: "right".into() });

    // ADC
    for i in 0..6 {
        let gpio = 32 + i;
        if gpio < gpio_count {
            pins.push(PinInfo {
                number: 13 + i,
                label: format!("GPIO{}/ADC", gpio),
                function: PinFunction::ADC,
                description: format!("ADC1_CH{}", i),
                side: if i < 3 { "left" } else { "right" }.into(),
            });
        }
    }

    // PWM-capable GPIOs
    for i in 0..4 {
        let gpio = 2 + i * 4;
        pins.push(PinInfo {
            number: 19 + i,
            label: format!("GPIO{}", gpio),
            function: PinFunction::PWM,
            description: format!("PWM Channel {}", i),
            side: if i < 2 { "left" } else { "right" }.into(),
        });
    }

    // General GPIOs
    for i in 0..4 {
        let gpio = 12 + i;
        pins.push(PinInfo {
            number: 23 + i,
            label: format!("GPIO{}", gpio),
            function: PinFunction::GPIO,
            description: "General GPIO".into(),
            side: if i < 2 { "left" } else { "right" }.into(),
        });
    }

    pins
}

fn arduino_uno_pins() -> Vec<PinInfo> {
    let mut pins = Vec::new();

    // Power
    pins.push(PinInfo { number: 0, label: "5V".into(), function: PinFunction::Power, description: "5V Power".into(), side: "left".into() });
    pins.push(PinInfo { number: 1, label: "GND".into(), function: PinFunction::Ground, description: "Ground".into(), side: "left".into() });
    pins.push(PinInfo { number: 2, label: "3.3V".into(), function: PinFunction::Power, description: "3.3V Power".into(), side: "left".into() });

    // Digital (PWM capable: D3, D5, D6, D9, D10, D11)
    let pwm_pins = [3u32, 5, 6, 9, 10, 11];
    for i in 0..14 {
        let func = if pwm_pins.contains(&i) { PinFunction::PWM } else { PinFunction::GPIO };
        let desc = if pwm_pins.contains(&i) { format!("Digital/PWM ~{}", i) } else { format!("Digital {}", i) };
        pins.push(PinInfo {
            number: 3 + i,
            label: format!("D{}", i),
            function: func,
            description: desc,
            side: if i < 7 { "left" } else { "right" }.into(),
        });
    }

    // UART (on D0/D1)
    pins.push(PinInfo { number: 17, label: "D0/RX".into(), function: PinFunction::UART, description: "UART RX".into(), side: "left".into() });
    pins.push(PinInfo { number: 18, label: "D1/TX".into(), function: PinFunction::UART, description: "UART TX".into(), side: "left".into() });

    // Analog
    for i in 0..6 {
        pins.push(PinInfo {
            number: 19 + i,
            label: format!("A{}", i),
            function: PinFunction::ADC,
            description: format!("Analog Input {}", i),
            side: "right".into(),
        });
    }

    // I2C
    pins.push(PinInfo { number: 25, label: "A4/SDA".into(), function: PinFunction::I2C, description: "I2C SDA".into(), side: "right".into() });
    pins.push(PinInfo { number: 26, label: "A5/SCL".into(), function: PinFunction::I2C, description: "I2C SCL".into(), side: "right".into() });

    // SPI
    pins.push(PinInfo { number: 27, label: "D10/SS".into(), function: PinFunction::SPI, description: "SPI SS".into(), side: "right".into() });
    pins.push(PinInfo { number: 28, label: "D11/MOSI".into(), function: PinFunction::SPI, description: "SPI MOSI".into(), side: "right".into() });
    pins.push(PinInfo { number: 29, label: "D12/MISO".into(), function: PinFunction::SPI, description: "SPI MISO".into(), side: "right".into() });
    pins.push(PinInfo { number: 30, label: "D13/SCK".into(), function: PinFunction::SPI, description: "SPI SCK".into(), side: "right".into() });

    pins
}

fn arduino_mega_pins() -> Vec<PinInfo> {
    let mut pins = arduino_uno_pins();
    // Add extra digital pins D14-D53
    for i in 14..54 {
        pins.push(PinInfo {
            number: 30 + (i - 14),
            label: format!("D{}", i),
            function: PinFunction::GPIO,
            description: format!("Digital {}", i),
            side: if i % 2 == 0 { "left" } else { "right" }.into(),
        });
    }
    pins
}

fn stm32_pins() -> Vec<PinInfo> {
    let mut pins = Vec::new();

    // Power
    pins.push(PinInfo { number: 0, label: "3V3".into(), function: PinFunction::Power, description: "3.3V Power".into(), side: "left".into() });
    pins.push(PinInfo { number: 1, label: "GND".into(), function: PinFunction::Ground, description: "Ground".into(), side: "left".into() });
    pins.push(PinInfo { number: 2, label: "5V".into(), function: PinFunction::Power, description: "5V (USB)".into(), side: "left".into() });

    // GPIO PA0-PA15
    for i in 0..16 {
        pins.push(PinInfo {
            number: 3 + i,
            label: format!("PA{}", i),
            function: PinFunction::GPIO,
            description: format!("Port A pin {}", i),
            side: if i < 8 { "left" } else { "right" }.into(),
        });
    }

    // UART
    pins.push(PinInfo { number: 19, label: "PA9/TX1".into(), function: PinFunction::UART, description: "USART1 TX".into(), side: "left".into() });
    pins.push(PinInfo { number: 20, label: "PA10/RX1".into(), function: PinFunction::UART, description: "USART1 RX".into(), side: "left".into() });

    // I2C
    pins.push(PinInfo { number: 21, label: "PB7/SDA".into(), function: PinFunction::I2C, description: "I2C1 SDA".into(), side: "right".into() });
    pins.push(PinInfo { number: 22, label: "PB6/SCL".into(), function: PinFunction::I2C, description: "I2C1 SCL".into(), side: "right".into() });

    // SPI
    pins.push(PinInfo { number: 23, label: "PA4/NSS".into(), function: PinFunction::SPI, description: "SPI1 NSS".into(), side: "right".into() });
    pins.push(PinInfo { number: 24, label: "PA5/SCK".into(), function: PinFunction::SPI, description: "SPI1 SCK".into(), side: "right".into() });
    pins.push(PinInfo { number: 25, label: "PA6/MISO".into(), function: PinFunction::SPI, description: "SPI1 MISO".into(), side: "right".into() });
    pins.push(PinInfo { number: 26, label: "PA7/MOSI".into(), function: PinFunction::SPI, description: "SPI1 MOSI".into(), side: "right".into() });

    // ADC
    for i in 0..4 {
        pins.push(PinInfo {
            number: 27 + i,
            label: format!("PA{}/ADC", i),
            function: PinFunction::ADC,
            description: format!("ADC1_CH{}", i),
            side: "right".into(),
        });
    }

    // PWM
    for i in 0..4 {
        pins.push(PinInfo {
            number: 31 + i,
            label: format!("PB{}/TIM", i),
            function: PinFunction::PWM,
            description: format!("TIM3_CH{}", i + 1),
            side: "right".into(),
        });
    }

    pins
}

fn pico_pins() -> Vec<PinInfo> {
    let mut pins = Vec::new();

    // Power
    pins.push(PinInfo { number: 0, label: "3V3".into(), function: PinFunction::Power, description: "3.3V Output".into(), side: "left".into() });
    pins.push(PinInfo { number: 1, label: "GND".into(), function: PinFunction::Ground, description: "Ground".into(), side: "left".into() });
    pins.push(PinInfo { number: 2, label: "VSYS".into(), function: PinFunction::Power, description: "Input Power".into(), side: "left".into() });

    // GP0-GP25
    for i in 0..26 {
        let func = if i < 4 { PinFunction::UART }
            else if i >= 4 && i < 8 { PinFunction::SPI }
            else if i >= 8 && i < 12 { PinFunction::I2C }
            else if i >= 26 { PinFunction::ADC }
            else { PinFunction::GPIO };
        let desc = match i {
            0 => "UART0 TX".into(),
            1 => "UART0 RX".into(),
            4 | 5 => format!("SPI0 {}", if i == 4 { "RX" } else { "SCK" }),
            16 | 17 => format!("SPI0 {}", if i == 16 { "RX" } else { "SCK" }),
            8 => "SPI1 RX".into(),
            9 => "SPI1 SCK".into(),
            _ => format!("GP{}", i),
        };
        pins.push(PinInfo {
            number: 3 + i,
            label: format!("GP{}", i),
            function: func,
            description: desc,
            side: if i < 13 { "left" } else { "right" }.into(),
        });
    }

    // ADC
    for i in 0..4 {
        pins.push(PinInfo {
            number: 29 + i,
            label: format!("ADC{}", i),
            function: PinFunction::ADC,
            description: format!("ADC Channel {}", i),
            side: "right".into(),
        });
    }

    pins
}

fn counpre64_pins() -> Vec<PinInfo> {
    let mut pins = Vec::new();

    pins.push(PinInfo { number: 0, label: "VCC".into(), function: PinFunction::Power, description: "3.3V".into(), side: "left".into() });
    pins.push(PinInfo { number: 1, label: "GND".into(), function: PinFunction::Ground, description: "Ground".into(), side: "left".into() });

    for i in 0..32 {
        let func = match i {
            0 | 1 => PinFunction::UART,
            2 | 3 | 4 => PinFunction::SPI,
            5 | 6 => PinFunction::I2C,
            _ => PinFunction::GPIO,
        };
        let desc = match i {
            0 => "UART TX".into(),
            1 => "UART RX".into(),
            2 => "SPI SS".into(),
            3 => "SPI SCK".into(),
            4 => "SPI MOSI".into(),
            5 => "I2C SDA".into(),
            6 => "I2C SCL".into(),
            _ => format!("GPIO {}", i),
        };
        pins.push(PinInfo {
            number: 2 + i,
            label: format!("IO{}", i),
            function: func,
            description: desc,
            side: if i < 16 { "left" } else { "right" }.into(),
        });
    }

    pins
}

fn generic_pins() -> Vec<PinInfo> {
    let mut pins = Vec::new();
    pins.push(PinInfo { number: 0, label: "VCC".into(), function: PinFunction::Power, description: "Power".into(), side: "left".into() });
    pins.push(PinInfo { number: 1, label: "GND".into(), function: PinFunction::Ground, description: "Ground".into(), side: "left".into() });
    for i in 0..16 {
        pins.push(PinInfo {
            number: 2 + i,
            label: format!("IO{}", i),
            function: PinFunction::GPIO,
            description: "General I/O".into(),
            side: if i < 8 { "left" } else { "right" }.into(),
        });
    }
    pins
}
