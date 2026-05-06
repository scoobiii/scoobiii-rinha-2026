#!/usr/bin/env python3
import gzip
import json
import math
from collections import Counter

N_DIMS = 14
N_SAMPLE = 5000  # menor para ficar mais rápido

def fetch_real_samples(n=N_SAMPLE):
    """Pega amostras do dataset real da Rinha"""
    samples = []
    with gzip.open('../rinha-de-backend-2026/resources/references.json.gz', 'rt') as f:
        for i, line in enumerate(f):
            if i >= n:
                break
            samples.append(json.loads(line))
    return samples

def euclidean_distance(a, b):
    """Distância euclidiana entre dois vetores"""
    return math.sqrt(sum((a[i] - b[i])**2 for i in range(len(a))))

def main():
    print("🎯 Teste de Quantização para Rinha 2026 (Python puro)")
    print("=" * 50)
    
    # Carrega amostras
    print(f"\n📦 Carregando {N_SAMPLE} amostras...")
    samples = fetch_real_samples(N_SAMPLE)
    vectors = [s['vector'] for s in samples]
    labels = [s['label'] for s in samples]
    
    fraud_count = sum(1 for l in labels if l == 'fraud')
    print(f"✓ Carregado: {len(vectors)} vetores, {fraud_count} fraudes")
    
    # Calcula tamanho em memória para 3M vetores
    print("\n📊 MEMÓRIA PARA 3 MILHÕES DE VETORES:")
    print(f"  Float32 (baseline): {3_000_000 * 14 * 4 / (1024*1024):.0f} MB")
    print(f"  Int8: {3_000_000 * 14 * 1 / (1024*1024):.0f} MB")
    print(f"  Binary (1-bit): {3_000_000 * ((14 + 7) // 8) / (1024*1024):.0f} MB")
    
    # Testa qualidade do Int8 (amostra pequena)
    print("\n🔬 Testando qualidade do Int8 (amostragem)...")
    
    # Simula quantização Int8
    def quant_int8(v):
        return [max(-127, min(127, int(x * 127))) for x in v]
    
    def unquant_int8(vq):
        return [x / 127.0 for x in vq]
    
    # Calcula erro médio de quantização
    total_error = 0
    for v in vectors[:1000]:
        vq = quant_int8(v)
        vr = unquant_int8(vq)
        error = euclidean_distance(v, vr)
        total_error += error
    
    avg_error = total_error / min(1000, len(vectors))
    print(f"  Erro médio por vetor (Int8): {avg_error:.6f}")
    
    # Testa KNN simples com Int8
    print("\n🔬 Testando KNN com Int8 (primeiros 200 vetores como query)...")
    
    hits = 0
    test_size = min(200, len(vectors))
    
    for i in range(test_size):
        query = vectors[i]
        query_int8 = quant_int8(query)
        actual_label = labels[i]
        
        # Calcula distância para todos os outros
        distances = []
        for j, ref in enumerate(vectors):
            if j == i:
                continue
            ref_int8 = quant_int8(ref)
            # Distância euclidiana no espaço Int8
            dist = math.sqrt(sum((query_int8[k] - ref_int8[k])**2 for k in range(N_DIMS)))
            distances.append((dist, labels[j]))
        
        # Pega os 5 vizinhos mais próximos
        distances.sort(key=lambda x: x[0])
        nearest_labels = [lbl for _, lbl in distances[:5]]
        
        # Se a maioria for igual ao label real, considera acerto
        if nearest_labels.count(actual_label) >= 3:
            hits += 1
    
    accuracy = hits / test_size if test_size > 0 else 0
    print(f"  Acurácia (match >=3 dos 5 vizinhos): {accuracy:.1%}")
    
    # Recomendação
    print("\n" + "=" * 50)
    print("📊 RECOMENDAÇÃO:")
    print("=" * 50)
    
    mem_int8 = 3_000_000 * 14 * 1 / (1024*1024)
    mem_binary = 3_000_000 * ((14 + 7) // 8) / (1024*1024)
    
    print(f"\nInt8: {mem_int8:.0f} MB, Acurácia: {accuracy:.1%}")
    print(f"Binary: {mem_binary:.0f} MB")
    
    if mem_int8 <= 256 and accuracy > 0.9:
        print("\n✅ RECOMENDADO: Int8")
        print("   • Cabe em {:.0f}MB (<256MB)".format(mem_int8))
        print("   • Mantém {:.1%} da qualidade".format(accuracy))
        print("   • Melhor que Binary (que perde muita informação)")
    elif mem_binary <= 256:
        print("\n⚠️  Int8 ultrapassou 256MB, use Binary com fallback")
    else:
        print("\n❌ Precisa de compressão adicional ou filtragem")

if __name__ == "__main__":
    main()
