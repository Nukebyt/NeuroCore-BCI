/* Host tests for the USB bridge core: COBS reference vectors and NIP forwarding. */
#include <string.h>

#include "bridge.h"
#include "cobs.h"
#include "minitest.h"
#include "nip.h"

typedef struct {
    size_t in_len;
    uint8_t in[256];
    size_t enc_len;
    uint8_t enc[260];
} cobs_vec_t;

static cobs_vec_t vecs[12];
static size_t n_vecs;

static void add_vec(const uint8_t *in, size_t in_len, const uint8_t *enc, size_t enc_len)
{
    memcpy(vecs[n_vecs].in, in, in_len);
    vecs[n_vecs].in_len = in_len;
    memcpy(vecs[n_vecs].enc, enc, enc_len);
    vecs[n_vecs].enc_len = enc_len;
    n_vecs++;
}

/* Same published examples as host/tests/test_serial_link.py. */
static void build_vectors(void)
{
    static const struct {
        size_t in_len;
        uint8_t in[4];
        size_t enc_len;
        uint8_t enc[5];
    } small[] = {
        {0, {0}, 1, {0x01}},
        {1, {0x00}, 2, {0x01, 0x01}},
        {2, {0x00, 0x00}, 3, {0x01, 0x01, 0x01}},
        {3, {0x00, 0x11, 0x00}, 4, {0x01, 0x02, 0x11, 0x01}},
        {4, {0x11, 0x22, 0x00, 0x33}, 5, {0x03, 0x11, 0x22, 0x02, 0x33}},
        {4, {0x11, 0x22, 0x33, 0x44}, 5, {0x05, 0x11, 0x22, 0x33, 0x44}},
        {4, {0x11, 0x00, 0x00, 0x00}, 5, {0x02, 0x11, 0x01, 0x01, 0x01}},
    };
    for (size_t i = 0; i < sizeof small / sizeof small[0]; i++) {
        add_vec(small[i].in, small[i].in_len, small[i].enc, small[i].enc_len);
    }
    uint8_t in[256];
    uint8_t enc[260];
    size_t k;

    /* 01..FE -> FF 01..FE */
    for (k = 0; k < 254; k++) {
        in[k] = (uint8_t)(k + 1u);
        enc[k + 1u] = (uint8_t)(k + 1u);
    }
    enc[0] = 0xFF;
    add_vec(in, 254, enc, 255);

    /* 00..FE -> 01 FF 01..FE */
    for (k = 0; k < 255; k++) {
        in[k] = (uint8_t)k;
    }
    enc[0] = 0x01;
    enc[1] = 0xFF;
    for (k = 0; k < 254; k++) {
        enc[k + 2u] = (uint8_t)(k + 1u);
    }
    add_vec(in, 255, enc, 256);

    /* 01..FF -> FF 01..FE 02 FF */
    for (k = 0; k < 255; k++) {
        in[k] = (uint8_t)(k + 1u);
    }
    enc[0] = 0xFF;
    for (k = 0; k < 254; k++) {
        enc[k + 1u] = (uint8_t)(k + 1u);
    }
    enc[255] = 0x02;
    enc[256] = 0xFF;
    add_vec(in, 255, enc, 257);

    /* 02..FF 00 -> FF 02..FF 01 01 */
    for (k = 0; k < 254; k++) {
        in[k] = (uint8_t)(k + 2u);
        enc[k + 1u] = (uint8_t)(k + 2u);
    }
    in[254] = 0x00;
    enc[0] = 0xFF;
    enc[255] = 0x01;
    enc[256] = 0x01;
    add_vec(in, 255, enc, 257);

    /* 03..FF 00 01 -> FE 03..FF 02 01 */
    for (k = 0; k < 253; k++) {
        in[k] = (uint8_t)(k + 3u);
        enc[k + 1u] = (uint8_t)(k + 3u);
    }
    in[253] = 0x00;
    in[254] = 0x01;
    enc[0] = 0xFE;
    enc[254] = 0x02;
    enc[255] = 0x01;
    add_vec(in, 255, enc, 256);
}

static void test_cobs_reference_vectors(void)
{
    for (size_t i = 0; i < n_vecs; i++) {
        uint8_t enc[COBS_MAX_ENCODED(256u)];
        size_t n = cobs_encode(vecs[i].in, vecs[i].in_len, enc, sizeof enc);
        MT_CHECK_EQ(n, vecs[i].enc_len);
        MT_CHECK(memcmp(enc, vecs[i].enc, vecs[i].enc_len) == 0);

        uint8_t dec[256];
        size_t dn = 0;
        MT_CHECK(cobs_decode(vecs[i].enc, vecs[i].enc_len, dec, sizeof dec, &dn));
        MT_CHECK_EQ(dn, vecs[i].in_len);
        MT_CHECK(memcmp(dec, vecs[i].in, vecs[i].in_len) == 0);
    }
}

static void test_cobs_rejects_malformed_and_small_buffers(void)
{
    uint8_t out[8];
    size_t n = 0;
    const uint8_t zero[] = {0x00};
    const uint8_t short_block[] = {0x05, 0x11};
    const uint8_t inner_zero[] = {0x03, 0x11, 0x00};
    MT_CHECK(!cobs_decode(zero, sizeof zero, out, sizeof out, &n));
    MT_CHECK(!cobs_decode(short_block, sizeof short_block, out, sizeof out, &n));
    MT_CHECK(!cobs_decode(inner_zero, sizeof inner_zero, out, sizeof out, &n));
    MT_CHECK(!cobs_decode(out, 0, out, sizeof out, &n));
    const uint8_t five[] = {0x06, 1, 2, 3, 4, 5};
    MT_CHECK(!cobs_decode(five, sizeof five, out, 4, &n));
    MT_CHECK_EQ(cobs_encode(five, sizeof five, out, 3), 0);
}

/* ---------------------------------------------------------------- bridge */

typedef struct {
    int count;
    uint8_t last[NIP_FRAME_LEN];
} sink_t;

static void sink(const uint8_t frame[NIP_FRAME_LEN], void *ctx)
{
    sink_t *s = (sink_t *)ctx;
    s->count++;
    memcpy(s->last, frame, NIP_FRAME_LEN);
}

static size_t make_msg(uint8_t type, const uint8_t *payload, size_t len, uint8_t *out)
{
    uint8_t raw[BRIDGE_MAX_RAW + 8];
    raw[0] = type;
    memcpy(&raw[1], payload, len);
    size_t n = cobs_encode(raw, len + 1u, out, COBS_MAX_ENCODED(BRIDGE_MAX_RAW + 8u));
    out[n] = 0x00;
    return n + 1u;
}

static void nip_bytes(uint8_t seq, uint8_t intent, uint8_t out[NIP_FRAME_LEN])
{
    nip_frame_t f = {.seq = seq, .t_ms = 1234, .intent = intent, .level = 1, .confidence = 9};
    MT_CHECK_EQ(nip_encode(&f, out), NIP_OK);
}

static void test_bridge_forwards_valid_nip_split_across_chunks(void)
{
    bridge_t b;
    bridge_init(&b);
    sink_t s = {0};
    uint8_t nipb[NIP_FRAME_LEN];
    nip_bytes(7, NIP_INTENT_FWD, nipb);
    uint8_t msg[64];
    size_t n = make_msg(BRIDGE_TYPE_NIP, nipb, NIP_FRAME_LEN, msg);
    for (size_t i = 0; i < n; i++) {
        bridge_rx_bytes(&b, &msg[i], 1, sink, &s); /* one byte at a time */
    }
    MT_CHECK_EQ(s.count, 1);
    MT_CHECK(memcmp(s.last, nipb, NIP_FRAME_LEN) == 0);
    MT_CHECK_EQ(b.forwarded, 1);
}

static void test_bridge_rejects_bad_crc_wrong_type_and_garbage(void)
{
    bridge_t b;
    bridge_init(&b);
    sink_t s = {0};
    uint8_t nipb[NIP_FRAME_LEN];
    uint8_t msg[64];
    size_t n;

    nip_bytes(1, NIP_INTENT_STOP, nipb);
    nipb[15] ^= 0x01; /* CRC broken */
    n = make_msg(BRIDGE_TYPE_NIP, nipb, NIP_FRAME_LEN, msg);
    bridge_rx_bytes(&b, msg, n, sink, &s);

    nip_bytes(2, NIP_INTENT_STOP, nipb);
    n = make_msg(BRIDGE_TYPE_STATUS, nipb, NIP_FRAME_LEN, msg); /* wrong type */
    bridge_rx_bytes(&b, msg, n, sink, &s);
    n = make_msg(BRIDGE_TYPE_NIP, nipb, 15, msg); /* wrong length */
    bridge_rx_bytes(&b, msg, n, sink, &s);

    const uint8_t garbage[] = {0x07, 0x07, 0x00, 0x00, 0x00};
    bridge_rx_bytes(&b, garbage, sizeof garbage, sink, &s);

    MT_CHECK_EQ(s.count, 0);
    MT_CHECK_EQ(b.rejected, 3);
    MT_CHECK_EQ(b.rx_errors, 1);

    /* Still works afterwards (resync). */
    n = make_msg(BRIDGE_TYPE_NIP, nipb, NIP_FRAME_LEN, msg);
    bridge_rx_bytes(&b, msg, n, sink, &s);
    MT_CHECK_EQ(s.count, 1);
}

static void test_bridge_drops_oversized_then_recovers(void)
{
    bridge_t b;
    bridge_init(&b);
    sink_t s = {0};
    uint8_t junk[200];
    memset(junk, 0x42, sizeof junk);
    bridge_rx_bytes(&b, junk, sizeof junk, sink, &s);
    const uint8_t delim = 0x00;
    bridge_rx_bytes(&b, &delim, 1, sink, &s);
    MT_CHECK_EQ(b.rx_errors, 1);

    uint8_t nipb[NIP_FRAME_LEN];
    uint8_t msg[64];
    nip_bytes(3, NIP_INTENT_HOLD, nipb);
    size_t n = make_msg(BRIDGE_TYPE_NIP, nipb, NIP_FRAME_LEN, msg);
    bridge_rx_bytes(&b, msg, n, sink, &s);
    MT_CHECK_EQ(s.count, 1);
}

static void test_bridge_forwards_each_frame_exactly_once(void)
{
    bridge_t b;
    bridge_init(&b);
    sink_t s = {0};
    uint8_t stream[64 * 30];
    size_t total = 0;
    for (uint8_t i = 0; i < 30; i++) {
        uint8_t nipb[NIP_FRAME_LEN];
        nip_bytes(i, NIP_INTENT_FWD, nipb);
        total += make_msg(BRIDGE_TYPE_NIP, nipb, NIP_FRAME_LEN, &stream[total]);
    }
    bridge_rx_bytes(&b, stream, total, sink, &s);
    MT_CHECK_EQ(s.count, 30);
    /* No input -> no output: the bridge has no way to emit frames on its own. */
    bridge_rx_bytes(&b, stream, 0, sink, &s);
    MT_CHECK_EQ(s.count, 30);
}

int main(void)
{
    build_vectors();
    MT_RUN(test_cobs_reference_vectors);
    MT_RUN(test_cobs_rejects_malformed_and_small_buffers);
    MT_RUN(test_bridge_forwards_valid_nip_split_across_chunks);
    MT_RUN(test_bridge_rejects_bad_crc_wrong_type_and_garbage);
    MT_RUN(test_bridge_drops_oversized_then_recovers);
    MT_RUN(test_bridge_forwards_each_frame_exactly_once);
    return MT_REPORT();
}
