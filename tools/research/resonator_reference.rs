// Host adapter for the external GPLv3 guitar-pitch-detection reference.
// Build beside a symlink named upstream pointing to the upstream src directory.
// The source and license remain in the external checkout.
#![allow(dead_code)]
#[path = "upstream/chord.rs"] mod chord;
#[path = "upstream/detector.rs"] mod detector;
#[path = "upstream/notes.rs"] mod notes;
#[path = "upstream/resonator.rs"] mod resonator;
#[path = "upstream/techniques.rs"] mod techniques;
#[path = "upstream/types.rs"] mod types;

// The original poly path does not use these outputs. This stub omits only
// Cycfi Q's independent mono and technique analysis, including its cost.
mod q_pitch {
    #[derive(Debug)]
    pub struct QPitchDetector;

    impl QPitchDetector {
        pub fn new_guitar(_: f32) -> Self { Self }
        pub fn process(&mut self, _: f32) {}
        pub fn frequency(&self) -> f32 { 0.0 }
        pub fn periodicity(&self) -> f32 { 0.0 }
        pub fn reset(&mut self) {}
    }
}

use detector::{DetectorConfig, GuitarPitchDetector};
use std::env;
use std::io::{self, Read, Write};

fn main() -> io::Result<()> {
    let args: Vec<String> = env::args().collect();
    if args.len() != 6 {
        eprintln!("usage: resonator-reference RATE HOP MIN_MIDI ALPHA ENERGY_THRESHOLD < input.f32");
        std::process::exit(2);
    }
    let rate = args[1].parse::<u32>().expect("invalid rate");
    let hop = args[2].parse::<usize>().expect("invalid hop");
    let config = DetectorConfig {
        sample_rate: rate,
        min_midi: args[3].parse().expect("invalid MIDI floor"),
        alpha: args[4].parse().expect("invalid alpha"),
        detection_threshold: args[5].parse().expect("invalid threshold"),
        ..DetectorConfig::default()
    };
    assert!(rate > 0 && hop > 0 && config.min_midi <= config.max_midi);
    assert!((0.0..1.0).contains(&config.alpha));
    assert!((0.0..=1.0).contains(&config.detection_threshold));
    let mut detector = GuitarPitchDetector::with_config_and_frame_size(config, hop);
    let mut input = Vec::new();
    io::stdin().read_to_end(&mut input)?;
    if input.len() % 4 != 0 {
        return Err(io::Error::new(io::ErrorKind::InvalidData, "expected LE float32 samples"));
    }
    let mut frame = vec![0.0_f32; hop];
    let mut out = io::BufWriter::new(io::stdout().lock());
    for (index, chunk) in input.chunks_exact(4 * hop).enumerate() {
        for (sample, bytes) in frame.iter_mut().zip(chunk.chunks_exact(4)) {
            *sample = f32::from_le_bytes(bytes.try_into().unwrap());
            if !sample.is_finite() {
                return Err(io::Error::new(io::ErrorKind::InvalidData, "nonfinite input"));
            }
        }
        let result = detector.process(&frame);
        write!(out, "{:.8}", (index + 1) as f64 * hop as f64 / rate as f64)?;
        for note in result.notes {
            write!(out, ",{:.5}", note.frequency)?;
        }
        writeln!(out)?;
    }
    out.flush()
}
