#!/usr/bin/env python3
import gzip
import json
import numpy as np
from collections import defaultdict

N_DIMS = 14
N_SAMPLE = 10000  # testar com 10k amostras

def fetch_real_samples(n=N_SAMPLE):
    """Pega amostras do dataset real da Rinha"""
    samples = []
    with gzip.open('../rinha-de-backend-2026/resources/references.json.gz', 'rt') as f:
        for i, line in enumerate(f):
            if i >= n:
                break
            samples.append(json.loads(line))
    return samples

def quantize_f32_to_int8(vec):
    """Converte vetor f32 (-1..1) para int8 (-127..127)"""
    return np.clip(vec * 127, -127, 127).astype(np.int8)

def quantize_to_binary(vec):
    """1-bit: >0 vira 1, <=0 vira -1"""
    return np.where(vec > 0, 1, -1).astype(np.int8)

def quantization_error(original, quantized):
    """Erro euclidiano médio por dimensão"""
    return np.sqrt(np.mean((original - quantized)**2))

def memory_mb(n_vectors, bytes_per_vec):
    return (n_vectors * bytes_per_vec) / (1024 * 1024)

def main():
    print("🎯 Teste de Quantização para Rinha 2026")
    print("=" * 50)
    
    # Carrega amostras reais
    print(f"\n📦 Carregando {N_SAMPLE} amostras do dataset real...")
    samples = fetch_real_samples(N_SAMPLE)
    vectors = np.array([s['vector'] for s in samples], dtype=np.float32)
    labels = [s['label'] for s in samples]
    
    print(f"✓ Carregado: {len(vectors)} vetores, {len([l for l in labels if l=='fraud'])} fraudes")
    
    # Testa cada quantização
    results = []
    
    for qtype, quant_func, bytes_per_vec, name in [
        ('float32', None, 4 * N_DIMS, 'Float32 (baseline)'),
        ('int8', quantize_f32_to_int8, 1 * N_DIMS, 'Int8'),
        ('binary', quantize_to_binary, (N_DIMS + 7) // 8, 'Binary (1-bit)'),
    ]:
        print(f"\n🔬 Testando: {name}")
        
        if qtype == 'float32':
            quantized = vectors
            error = 0.0
        else:
            quantized = np.array([quant_func(v) for v in vectors])
            # Reconstrói para float32 para calcular erro
            reconstructed = quantized.astype(np.float32) / (127.0 if qtype == 'int8' else 1.0)
            error = quantization_error(vectors, reconstructed)
        
        memory = memory_mb(3_000_000, bytes_per_vec)
        
        # Testa busca KNN (simplificada) com primeiros 100 vetores como queries
        if qtype != 'float32':
            correct = 0
            total = 0
            # Amostra pequena para teste de qualidade
            n_query = min(500, len(vectors))
            for i in range(n_query):
                query = vectors[i]
                query_q = quant_func(query)
                
                # Calcula distância para todos (simplificado)
                distances = []
                for j, ref in enumerate(vectors):
                    if j == i:
                        continue
                    if qtype == 'int8':
                        ref_q = quant_func(ref)
                        dist = np.linalg.norm(query_q.astype(np.float32) - ref_q.astype(np.float32))
                    else:  # binary
                        # Hamming distance (bits diferentes)
                        diff = (query_q != quant_func(ref)).sum()
                        dist = diff / N_DIMS
                    distances.append((dist, labels[j]))
                
                distances.sort(key=lambda x: x[0])
                nearest_labels = [lbl for _, lbl in distances[:5]]
                actual_label = labels[i]
                
                # Se 3+ dos 5 vizinhos têm mesmo label, acertou
                if nearest_labels.count(actual_label) >= 3:
                    correct += 1
                total += 1
            
            accuracy = correct / total
        else:
            accuracy = 1.0
        
        results.append({
            'name': name,
            'error': error,
            'memory_mb': memory,
            'accuracy': accuracy,
            'bytes_per_vec': bytes_per_vec
        })
        
        print(f"  Erro médio por dimensão: {error:.6f}")
        print(f"  Bytes/vetor: {bytes_per_vec}")
        print(f"  Memória para 3M vetores: {memory:.1f} MB")
        if qtype != 'float32':
            print(f"  Acurácia KNN (@5 com match>=3): {accuracy:.1%}")
    
    # Recomendação final
    print("\n" + "=" * 50)
    print("📊 RESULTADO FINAL E RECOMENDAÇÃO:")
    print("=" * 50)
    
    for r in results:
        print(f"\n{r['name']}:")
        print(f"  • Memória: {r['memory_mb']:.0f} MB")
        print(f"  • Erro: {r['error']:.6f}")
        if r['name'] != 'Float32 (baseline)':
            print(f"  • Acurácia: {r['accuracy']:.1%}")
            if r['accuracy'] > 0.95 and r['memory_mb'] < 200:
                print("  ✅ RECOMENDADO para Rinha 2026")
            elif r['accuracy'] < 0.85:
                print("  ❌ PERDA DE QUALIDADE MUITO ALTA")
    
    # Verifica limite de 256MB
    print("\n🔍 VERIFICAÇÃO DO LIMITE (256MB):")
    for r in results:
        if r['memory_mb'] <= 256:
            print(f"  ✅ {r['name']} → {r['memory_mb']:.0f} MB (dentro do limite)")
        else:
            print(f"  ❌ {r['name']} → {r['memory_mb']:.0f} MB (ultrapassa)")

if __name__ == "__main__":
    main()
