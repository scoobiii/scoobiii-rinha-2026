mod quantization;
use quantization::*;

fn main() {
    println!("🎯 Teste de Quantização para Rinha 2026");
    println!("=======================================\n");
    
    let example_vec: [f32; N_DIMS] = [
        0.9506, 0.8333, 1.0, 0.2174, 0.8333, -1.0, -1.0, 0.9523, 1.0, 0.0, 1.0, 1.0, 0.75, 0.0055
    ];
    
    println!("Vetor original (primeiras 5 dimensões):");
    println!("  {:?}\n", &example_vec[..5]);
    
    for qtype in [QuantType::Float16, QuantType::Int8, QuantType::Binary] {
        let error = quantization_error(&example_vec, qtype);
        let bytes_per_vec = match qtype {
            QuantType::Float16 => N_DIMS * 2,
            QuantType::Int8 => N_DIMS * 1,
            QuantType::Binary => (N_DIMS + 7) / 8,
            _ => 0,
        };
        let total_mb = (bytes_per_vec * N_VECTORS) as f64 / (1024.0 * 1024.0);
        println!("{:?}:", qtype);
        println!("  Erro médio: {:.6}", error);
        println!("  Bytes/vetor: {}", bytes_per_vec);
        println!("  Memória 3M vetores: {:.1} MB\n", total_mb);
    }
}
