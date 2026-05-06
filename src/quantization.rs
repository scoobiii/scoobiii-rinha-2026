use anyhow::Result;
use ndarray::Array1;
use rand::Rng;

pub const N_DIMS: usize = 14;
pub const N_VECTORS: usize = 3_000_000;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum QuantType {
    Float32,
    Float16,
    Int8,
    Binary,
}

pub fn quantize_f32_to(vec: &[f32; N_DIMS], qtype: QuantType) -> Vec<u8> {
    match qtype {
        QuantType::Float32 => vec.iter().flat_map(|&x| x.to_le_bytes()).collect(),
        QuantType::Float16 => vec.iter().flat_map(|&x| f32_to_f16(x).to_le_bytes()).collect(),
        QuantType::Int8 => vec.iter().map(|&x| ((x * 127.0).clamp(-127.0, 127.0) as i8).to_le_bytes()).flatten().collect(),
        QuantType::Binary => vec.iter().map(|&x| if x > 0.0 { 1u8 } else { 0u8 }).collect(),
    }
}

fn f32_to_f16(x: f32) -> u16 {
    ((x * 65535.0) as u16).clamp(0, 65535)
}

pub fn euclidean_distance(a: &[f32; N_DIMS], b: &[f32; N_DIMS]) -> f32 {
    a.iter().zip(b.iter()).map(|(x, y)| (x - y).powi(2)).sum::<f32>().sqrt()
}

pub fn quantization_error(original: &[f32; N_DIMS], qtype: QuantType) -> f32 {
    let quantized = quantize_to_f32(original, qtype);
    euclidean_distance(original, &quantized)
}

fn quantize_to_f32(orig: &[f32; N_DIMS], qtype: QuantType) -> [f32; N_DIMS] {
    let mut out = [0.0; N_DIMS];
    match qtype {
        QuantType::Float32 => return *orig,
        QuantType::Float16 => {
            for i in 0..N_DIMS {
                let f16_bits = f32_to_f16(orig[i]);
                out[i] = (f16_bits as f32) / 65535.0 * 2.0 - 1.0;
            }
        }
        QuantType::Int8 => {
            for i in 0..N_DIMS {
                let i8_val = ((orig[i] * 127.0).clamp(-127.0, 127.0) as i8);
                out[i] = i8_val as f32 / 127.0;
            }
        }
        QuantType::Binary => {
            for i in 0..N_DIMS {
                out[i] = if orig[i] > 0.0 { 1.0 } else { -1.0 };
            }
        }
    }
    out
}
