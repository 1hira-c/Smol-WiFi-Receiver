// SPDX-License-Identifier: MIT OR Apache-2.0

use smol_receiver_bridge::hub::ReceiverHub;
use smol_receiver_bridge::inputs::{GatewayInput, HidInput};
use smol_receiver_bridge::slimevr::SlimeVrOutput;
use std::error::Error;
use std::net::SocketAddr;
use std::thread;
use std::time::{Duration, Instant};

const DEFAULT_GATEWAY_LISTEN: &str = "0.0.0.0:6970";
const DEFAULT_SERVER: &str = "127.0.0.1:6969";
const DEFAULT_HID_VID: u16 = 0x1209;
const DEFAULT_HID_PID: u16 = 0x7690;

#[derive(Debug)]
struct Config {
    gateway_listen: SocketAddr,
    server: SocketAddr,
    hid_products: Vec<(u16, u16)>,
    hid_enabled: bool,
}

fn main() -> Result<(), Box<dyn Error>> {
    let config = parse_args()?;
    let mut gateway = GatewayInput::bind(config.gateway_listen)?;
    let mut hid = if config.hid_enabled {
        Some(HidInput::new(config.hid_products.clone())?)
    } else {
        None
    };
    let mut hub = ReceiverHub::default();
    let mut output = SlimeVrOutput::new(config.server);
    let started = Instant::now();
    let mut last_status = started;

    println!("Smol Receiver Bridge {}", env!("CARGO_PKG_VERSION"));
    println!("SVW1 listening on {}", gateway.local_address()?);
    println!("SlimeVR Server output: {}", config.server);
    if config.hid_enabled {
        for (vid, pid) in &config.hid_products {
            println!("HID input: {vid:04X}:{pid:04X}");
        }
    }

    loop {
        let now = Instant::now();
        let now_ms = started.elapsed().as_millis().min(u128::from(u64::MAX)) as u64;
        gateway.poll(&mut hub, &mut output, now_ms, now)?;
        if let Some(hid) = &mut hid {
            hid.poll(&mut hub, &mut output, now_ms, now);
        }
        output.tick(now);
        if now.duration_since(last_status) >= Duration::from_secs(5) {
            let hid_count = hid.as_ref().map_or(0, HidInput::device_count);
            println!(
                "status: HID receivers={hid_count}, logical trackers={}",
                output.tracker_count()
            );
            for (source, metrics) in hub.metrics() {
                println!(
                    "  {source}: rx={} accepted={} duplicate={} stale={} missing={} crc={}",
                    metrics.received,
                    metrics.accepted,
                    metrics.duplicates,
                    metrics.stale,
                    metrics.sequence_missing,
                    metrics.crc_errors,
                );
            }
            last_status = now;
        }
        thread::sleep(Duration::from_millis(1));
    }
}

fn parse_args() -> Result<Config, Box<dyn Error>> {
    let mut gateway_listen = DEFAULT_GATEWAY_LISTEN.parse()?;
    let mut server = DEFAULT_SERVER.parse()?;
    let mut hid_products = Vec::new();
    let mut hid_enabled = true;
    let mut args = std::env::args().skip(1);
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--gateway-listen" => gateway_listen = required_value(&mut args, &arg)?.parse()?,
            "--server" => server = required_value(&mut args, &arg)?.parse()?,
            "--hid" => hid_products.push(parse_vid_pid(&required_value(&mut args, &arg)?)?),
            "--no-hid" => hid_enabled = false,
            "-h" | "--help" => {
                print_help();
                std::process::exit(0);
            }
            _ => return Err(format!("unknown argument: {arg}").into()),
        }
    }
    if hid_products.is_empty() {
        hid_products.push((DEFAULT_HID_VID, DEFAULT_HID_PID));
    }
    Ok(Config {
        gateway_listen,
        server,
        hid_products,
        hid_enabled,
    })
}

fn required_value(
    args: &mut impl Iterator<Item = String>,
    option: &str,
) -> Result<String, Box<dyn Error>> {
    args.next()
        .ok_or_else(|| format!("{option} requires a value").into())
}

fn parse_vid_pid(value: &str) -> Result<(u16, u16), Box<dyn Error>> {
    let (vid, pid) = value.split_once(':').ok_or("HID product must be VID:PID")?;
    Ok((parse_hex_u16(vid)?, parse_hex_u16(pid)?))
}

fn parse_hex_u16(value: &str) -> Result<u16, Box<dyn Error>> {
    Ok(u16::from_str_radix(value.trim_start_matches("0x"), 16)?)
}

fn print_help() {
    println!("Usage: smol-receiver-bridge [options]");
    println!();
    println!("  --gateway-listen ADDRESS  SVW1 listen address (default {DEFAULT_GATEWAY_LISTEN})");
    println!("  --server ADDRESS          SlimeVR Server address (default {DEFAULT_SERVER})");
    println!("  --hid VID:PID             HID receiver product; repeat for more products");
    println!("  --no-hid                  Disable USB HID input");
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_hex_hid_product() {
        assert_eq!(parse_vid_pid("1209:7690").unwrap(), (0x1209, 0x7690));
        assert_eq!(parse_vid_pid("0x1209:0x7690").unwrap(), (0x1209, 0x7690));
    }
}
