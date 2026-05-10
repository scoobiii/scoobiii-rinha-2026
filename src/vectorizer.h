#pragma once
#include <stdint.h>
#include <string.h>

#define DIMS 14
#define Q_FACTOR 32767.0f

/* 
 * Vetor quantizado (int16_t):
 * Reduz o uso de cache em 50% e permite 8/16 operações por ciclo via SIMD.
 */
typedef int16_t vec_t;

/* Constantes de Normalização */
#define MAX_AMOUNT          10000.0f
#define MAX_INSTALLMENTS    12.0f
#define AMOUNT_VS_AVG_RATIO 10.0f
#define MAX_MINUTES         1440.0f
#define MAX_KM              1000.0f
#define MAX_TX_COUNT_24H    20.0f
#define MAX_MERCHANT_AVG    10000.0f

/* 
 * Fast MCC Lookup: Compara 4 bytes como um inteiro de 32 bits.
 * Muito mais rápido que múltiplos strcmps.
 */
static inline float fast_mcc_risk(const char *mcc) {
    if (!mcc || !mcc[0]) return 0.5f;
    uint32_t m = *((uint32_t*)mcc);
    switch(m) {
        case 0x31313435: return 0.15f; // "5411"
        case 0x32313835: return 0.30f; // "5812"
        case 0x32313935: return 0.20f; // "5912"
        case 0x34343935: return 0.45f; // "5944"
        case 0x31303837: return 0.80f; // "7801"
        case 0x32303837: return 0.75f; // "7802"
        case 0x35393937: return 0.85f; // "7995"
        case 0x31313534: return 0.35f; // "4511"
        case 0x31313335: return 0.25f; // "5311"
        case 0x39393935: return 0.50f; // "5999"
        default: return 0.5f;
    }
}

/* 
 * Fast ISO8601 Parser:
 * Converte "2026-03-11T20:23:35Z" para Epoch e extrai Hour/DoW.
 * Sem sscanf, sem mktime, sem locks de timezone.
 */
static inline void fast_parse_iso(const char *ts, int *h, int *dow, long *epoch) {
    if (!ts || ts[0] == '\0') return;
    
    int y = (ts[0]-'0')*1000 + (ts[1]-'0')*100 + (ts[2]-'0')*10 + (ts[3]-'0');
    int m = (ts[5]-'0')*10 + (ts[6]-'0');
    int d = (ts[8]-'0')*10 + (ts[9]-'0');
    *h    = (ts[11]-'0')*10 + (ts[12]-'0');
    int min = (ts[14]-'0')*10 + (ts[15]-'0');
    int sec = (ts[17]-'0')*10 + (ts[18]-'0');

    // Algoritmo de Sakamoto para Day of Week
    static int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    int y_dow = y - (m < 3);
    *dow = (y_dow + y_dow/4 - y_dow/100 + y_dow/400 + t[m-1] + d) % 7;
    // Ajuste para Mon=0 ... Sun=6
    *dow = (*dow == 0) ? 6 : *dow - 1;

    // Unix Epoch simplificado (Era 2000+)
    if (m < 3) { y--; m += 12; }
    *epoch = (long)(365*y + y/4 - y/100 + y/400 + (m*306 + 5)/10 + (d - 1)) * 86400L + (*h*3600) + (min*60) + sec;
}

static inline float f_clamp01(float x) {
    return (x < 0.0f) ? 0.0f : (x > 1.0f) ? 1.0f : x;
}

/*
 * Build Vector Quantized:
 * Gera o vetor de 14 dimensões já escalado para int16.
 */
static inline void build_vector_q(
    vec_t *vec,
    float amount, int installments, const char *requested_at,
    float cust_avg_amount, int tx_count_24h, int merchant_is_known,
    const char *mcc, float merch_avg_amount,
    int is_online, int card_present, float km_from_home,
    int has_last_tx, const char *last_ts, float km_from_current
) {
    int hour, dow;
    long current_epoch;
    fast_parse_iso(requested_at, &hour, &dow, &current_epoch);

    float tmp[DIMS];

    tmp[0]  = f_clamp01(amount / MAX_AMOUNT);
    tmp[1]  = f_clamp01((float)installments / MAX_INSTALLMENTS);
    tmp[2]  = (cust_avg_amount > 0.0f) ? f_clamp01((amount / cust_avg_amount) / AMOUNT_VS_AVG_RATIO) : 1.0f;
    tmp[3]  = (float)hour / 23.0f;
    tmp[4]  = (float)dow  / 6.0f;

    // Dim 5: Minutes since last tx
    if (!has_last_tx) {
        tmp[5] = -1.0f;
    } else {
        int lh, ld; long last_epoch;
        fast_parse_iso(last_ts, &lh, &ld, &last_epoch);
        float diff_m = (float)(current_epoch - last_epoch) / 60.0f;
        if (diff_m < 0) diff_m = -diff_m; // fabs manual
        tmp[5] = f_clamp01(diff_m / MAX_MINUTES);
    }

    tmp[6]  = (!has_last_tx) ? -1.0f : f_clamp01(km_from_current / MAX_KM);
    tmp[7]  = f_clamp01(km_from_home / MAX_KM);
    tmp[8]  = f_clamp01((float)tx_count_24h / MAX_TX_COUNT_24H);
    tmp[9]  = is_online    ? 1.0f : 0.0f;
    tmp[10] = card_present ? 1.0f : 0.0f;
    tmp[11] = merchant_is_known ? 0.0f : 1.0f;
    tmp[12] = fast_mcc_risk(mcc);
    tmp[13] = f_clamp01(merch_avg_amount / MAX_MERCHANT_AVG);

    // Quantização para int16 (SIMD friendly)
    for(int i = 0; i < DIMS; i++) {
        vec[i] = (vec_t)(tmp[i] * Q_FACTOR);
    }
}
