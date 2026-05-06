use criterion::{black_box, criterion_group, criterion_main, Criterion};
use rinha_quant_bench::quantization::*;
use rand::prelude::*;
use usearch::prelude::*;

fn generate_random_vector() -> [f32; N_DIMS] {
    let mut rng = thread_rng();
    let mut vec = [0.0; N_DIMS];
    for i in 0..N_DIMS {
        vec[i] = rng.gen_range(-1.0..1.0);
    }
    vec
}

fn bench_quantization(c: &mut Criterion) {
    let mut group = c.benchmark_group("quantization");
    
    println!("\n📊 Tamanho do índice para 3M vetores:");
    for qtype in [QuantType::Float32, QuantType::Float16, QuantType::Int8, QuantType::Binary] {
        let bytes_per_vector = match qtype {
            QuantType::Float32 => N_DIMS * 4,
            QuantType::Float16 => N_DIMS * 2,
            QuantType::Int8 => N_DIMS * 1,
            QuantType::Binary => (N_DIMS + 7) / 8,
        };
        let total_mb = (bytes_per_vector * N_VECTORS) as f64 / (1024.0 * 1024.0);
        println!("  {:?}: {:.1} MB", qtype, total_mb);
    }
    
    let sample_size = 1000;
    let vectors: Vec<[f32; N_DIMS]> = (0..sample_size).map(|_| generate_random_vector()).collect();
    let query = generate_random_vector();
    
    for qtype in [QuantType::Float32, QuantType::Int8] {
        let mut index = Index::new(usearch::MetricKind::L2, 
            match qtype {
                QuantType::Float32 => usearch::ScalarKind::F32,
                QuantType::Int8 => usearch::ScalarKind::I8,
                _ => continue,
            }, N_DIMS).unwrap();
        
        for (id, vec) in vectors.iter().enumerate() {
            let quantized = quantize_f32_to(vec, qtype);
            index.add(id as u32, &quantized).unwrap();
        }
        
        let query_quantized = quantize_f32_to(&query, qtype);
        
        group.bench_function(format!("{:?}_search_{}_vectors", qtype, sample_size), |b| {
            b.iter(|| {
                let mut results = vec![0u32; 5];
                let mut distances = vec![0f32; 5];
                index.search(&query_quantized, 5, &mut results, &mut distances).unwrap();
                black_box(&results);
            })
        });
    }
    
    group.finish();
}

criterion_group!(benches, bench_quantization);
criterion_main!(benches);
