#include "main.h"
#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>
LOG_MODULE_REGISTER(daydream, LOG_LEVEL_DBG);

#define DAYDREAM_THREAD_STACK_SIZE 1024
#define DAYDREAM_THREAD_PRIORITY 0


K_MSGQ_DEFINE(daydream_pkt_queue, DAYDREAM_PKT_SIZE, 8, sizeof(void*));


int daydream_queue_pkt(uint8_t const *pkt, k_timeout_t timeout)
{
    return k_msgq_put(&daydream_pkt_queue, pkt, timeout);
}

static uint32_t decode_inner(uint8_t *pkt, size_t *nbitsp,
    size_t start_byte, size_t start_bit, size_t end_byte, size_t end_bit)
{
    uint32_t result = 0;

    for (size_t i = start_byte; i <= end_byte; ++i) {
        result <<= 8;
        result |= pkt[i];
    }

    uint32_t const nbits = ((end_byte - start_byte) * 8) + start_bit - end_bit;
    uint32_t const mask = (1u << nbits) - 1u;

    __ASSERT(nbits < 32, "decode_inner nbits <= 32");

    if (nbitsp) {
        *nbitsp = nbits;
    }

    result >>= end_bit;
    result &= mask;

    return result;
}

static unsigned decode_unsigned(uint8_t *pkt,
    size_t start_byte, size_t start_bit, size_t end_byte, size_t end_bit)
{
    return decode_inner(pkt, NULL,
        start_byte, start_bit, end_byte, end_bit);
}

static int decode_twos_complement(uint8_t *pkt,
    size_t start_byte, size_t start_bit, size_t end_byte, size_t end_bit)
{
    size_t nbits = 0;
    uint32_t decoded = decode_inner(pkt, &nbits,
        start_byte, start_bit, end_byte, end_bit);

    __ASSERT(nbits > 0, "decode_twos_complement nbits > 0");
    __ASSERT(nbits < 32, "decode_twos_complement nbits < 32");

    uint32_t const signmask = 1u << (nbits - 1);

    int result;

    if (decoded & signmask) {
        uint32_t const mask = (1u << nbits) - 1u;
        // result = 1;
        result = (~decoded & mask);
        result += 1;
        result = -result;
    } else {
        result = decoded;
    }

    return result;
}

static int daydream_decode(void *_a, void *_b, void *_c)
{
    bool has_initial = false;
    unsigned prev_timestamp;

    uint8_t pkt[DAYDREAM_PKT_SIZE];
    struct daydream_pkt decoded = {};
    int err;

    for (;;) {
        err = k_msgq_get(&daydream_pkt_queue, pkt, K_MSEC(500));
        if (!bluetooth_is_connected()) {
            k_msgq_purge(&daydream_pkt_queue);
            continue;
        }

        if (err == -EAGAIN && bluetooth_is_connected()) {
            LOG_WRN("Packet timeout");
            continue;
        } else if (err) {
            LOG_ERR("k_msgq_get: %d", err);
            continue;
        }

        unsigned sqn = decode_unsigned(pkt, 1, 7, 1, 2);
        unsigned timestamp = decode_unsigned(pkt, 0, 8, 1, 7);

        if (has_initial) {
            if ((decoded.sqn + 1) % 32 != sqn) {
                LOG_WRN("Dropped packet? prev_sqn=%u sqn=%u", decoded.sqn, sqn);
            }

            if (timestamp <= prev_timestamp) {
                decoded.duration = 512 - prev_timestamp + timestamp;
            } else {
                decoded.duration = timestamp - prev_timestamp;
            }

            decoded.orient_x = decode_twos_complement(pkt, 1, 2, 3, 5);
            decoded.orient_z = decode_twos_complement(pkt, 3, 5, 4, 0);
            decoded.orient_y = -decode_twos_complement(pkt, 5, 8, 6, 3);

            decoded.accel_x = decode_twos_complement(pkt, 6, 3, 8, 6);
            decoded.accel_z = decode_twos_complement(pkt, 8, 6, 9, 1);
            decoded.accel_y = -decode_twos_complement(pkt, 9, 1, 11, 4);

            decoded.gyro_x = decode_twos_complement(pkt, 11, 4, 13, 7);
            decoded.gyro_z = decode_twos_complement(pkt, 13, 7, 14, 2);
            decoded.gyro_y = -decode_twos_complement(pkt, 14, 2, 16, 5);

            decoded.trackpad_x = decode_unsigned(pkt, 16, 5, 17, 5);
            decoded.trackpad_y = decode_unsigned(pkt, 17, 5, 18, 5);

            decoded.sqn = sqn;

            decoded.vol_up = (pkt[18] & 0x10) != 0;
            decoded.vol_dn = (pkt[18] & 0x08) != 0;
            decoded.app = (pkt[18] & 0x04) != 0;
            decoded.home = (pkt[18] & 0x02) != 0;
            decoded.trackpad_btn = (pkt[18] & 0x01) != 0;

            err = mouse_push_daydream(&decoded);
            if (err) {
                LOG_ERR("mouse_push_daydream: %d", err);
            }
        }

        has_initial = true;
        prev_timestamp = timestamp;
    }

    return 0;
}

K_THREAD_DEFINE(daydream_decode_thread, DAYDREAM_THREAD_STACK_SIZE,
    daydream_decode, NULL, NULL, NULL,
    DAYDREAM_THREAD_PRIORITY, 0, 1);
