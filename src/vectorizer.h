#pragma once
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define DIMS 14

/* mcc_risk table (hardcoded from mcc_risk.json) */
static inline float mcc_risk_lookup(const char *mcc) {
    if (!mcc || !*mcc) return 0.5f;
    if (strcmp(mcc, "5411") == 0) return 0.15f;
    if (strcmp(mcc, "5812") == 0) return 0.30f;
    if (strcmp(mcc, "5912") == 0) return 0.20f;
    if (strcmp(mcc, "5944") == 0) return 0.45f;
    if (strcmp(mcc, "7801") == 0) return 0.80f;
    if (strcmp(mcc, "7802") == 0) return 0.75f;
    if (strcmp(mcc, "7995") == 0) return 0.85f;
    if (strcmp(mcc, "4511") == 0) return 0.35f;
    if (strcmp(mcc, "5311") == 0) return 0.25f;
    if (strcmp(mcc, "5999") == 0) return 0.50f;
    return 0.5f;
}

/* normalization.json constants */
#define MAX_AMOUNT          10000.0f
#define MAX_INSTALLMENTS    12.0f
#define AMOUNT_VS_AVG_RATIO 10.0f
#define MAX_MINUTES         1440.0f
#define MAX_KM              1000.0f
#define MAX_TX_COUNT_24H    20.0f
#define MAX_MERCHANT_AVG    10000.0f

static inline float clamp01(float x) {
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

/* Parse ISO8601 UTC → hour (0-23) and dow (mon=0, sun=6) */
static inline void parse_iso8601(const char *ts, int *hour, int *dow) {
    *hour = 0; *dow = 0;
    if (!ts || strlen(ts) < 19) return;
    struct tm t = {0};
    sscanf(ts, "%4d-%2d-%2dT%2d:%2d:%2d",
           &t.tm_year, &t.tm_mon, &t.tm_mday,
           &t.tm_hour, &t.tm_min, &t.tm_sec);
    t.tm_year -= 1900;
    t.tm_mon  -= 1;
    t.tm_isdst = 0;
    mktime(&t);
    *hour = t.tm_hour;
    /* tm_wday: 0=sun..6=sat → rinha: mon=0..sun=6 */
    *dow = (t.tm_wday == 0) ? 6 : t.tm_wday - 1;
}

/* Minutes between two ISO8601 timestamps */
static inline float minutes_between(const char *ts_prev, const char *ts_cur) {
    if (!ts_prev || !*ts_prev || !ts_cur || !*ts_cur) return -1.0f;
    struct tm t1 = {0}, t2 = {0};
    if (sscanf(ts_prev, "%4d-%2d-%2dT%2d:%2d:%2d",
               &t1.tm_year, &t1.tm_mon, &t1.tm_mday,
               &t1.tm_hour, &t1.tm_min, &t1.tm_sec) != 6) return -1.0f;
    if (sscanf(ts_cur,  "%4d-%2d-%2dT%2d:%2d:%2d",
               &t2.tm_year, &t2.tm_mon, &t2.tm_mday,
               &t2.tm_hour, &t2.tm_min, &t2.tm_sec) != 6) return -1.0f;
    t1.tm_year -= 1900; t1.tm_mon -= 1; t1.tm_isdst = 0;
    t2.tm_year -= 1900; t2.tm_mon -= 1; t2.tm_isdst = 0;
    time_t e1 = mktime(&t1);
    time_t e2 = mktime(&t2);
    double diff = difftime(e2, e1);
    return (float)(fabs(diff) / 60.0);
}

/*
 * Build the 14-dim vector from parsed transaction fields.
 *
 * Params match exactly the 14 dimensions in REGRAS_DE_DETECCAO.md:
 *   0  amount
 *   1  installments
 *   2  amount_vs_avg
 *   3  hour_of_day
 *   4  day_of_week
 *   5  minutes_since_last_tx  (-1 if no prior tx)
 *   6  km_from_last_tx        (-1 if no prior tx)
 *   7  km_from_home
 *   8  tx_count_24h
 *   9  is_online
 *  10  card_present
 *  11  unknown_merchant
 *  12  mcc_risk
 *  13  merchant_avg_amount
 */
static inline void build_vector(
    float *vec,
    /* transaction */
    float amount, int installments, const char *requested_at,
    /* customer */
    float cust_avg_amount, int tx_count_24h,
    int merchant_is_known,          /* 1 if known, 0 if unknown */
    /* merchant */
    const char *mcc, float merch_avg_amount,
    /* terminal */
    int is_online, int card_present, float km_from_home,
    /* last_transaction (pass NULL/negative if absent) */
    int has_last_tx,
    const char *last_ts,            /* last_transaction.timestamp */
    float km_from_current           /* last_transaction.km_from_current */
) {
    int hour = 0, dow = 0;
    parse_iso8601(requested_at, &hour, &dow);

    /* dim 0 */ vec[0]  = clamp01(amount / MAX_AMOUNT);
    /* dim 1 */ vec[1]  = clamp01((float)installments / MAX_INSTALLMENTS);
    /* dim 2 */ {
        float ratio = (cust_avg_amount > 0.0f)
            ? (amount / cust_avg_amount) / AMOUNT_VS_AVG_RATIO
            : 1.0f;
        vec[2] = clamp01(ratio);
    }
    /* dim 3 */ vec[3]  = (float)hour / 23.0f;
    /* dim 4 */ vec[4]  = (float)dow  / 6.0f;
    /* dim 5 */ {
        if (!has_last_tx) {
            vec[5] = -1.0f;
        } else {
            float mins = minutes_between(last_ts, requested_at);
            vec[5] = (mins < 0) ? -1.0f : clamp01(mins / MAX_MINUTES);
        }
    }
    /* dim 6 */ {
        if (!has_last_tx) vec[6] = -1.0f;
        else              vec[6] = clamp01(km_from_current / MAX_KM);
    }
    /* dim 7  */ vec[7]  = clamp01(km_from_home / MAX_KM);
    /* dim 8  */ vec[8]  = clamp01((float)tx_count_24h / MAX_TX_COUNT_24H);
    /* dim 9  */ vec[9]  = is_online    ? 1.0f : 0.0f;
    /* dim 10 */ vec[10] = card_present ? 1.0f : 0.0f;
    /* dim 11 */ vec[11] = merchant_is_known ? 0.0f : 1.0f; /* 1 = unknown */
    /* dim 12 */ vec[12] = mcc_risk_lookup(mcc);
    /* dim 13 */ vec[13] = clamp01(merch_avg_amount / MAX_MERCHANT_AVG);
}
