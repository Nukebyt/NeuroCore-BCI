/* T1.1 acceptance tests for the C NIP codec. Same vectors as the Python codec. */
#include <string.h>

#include "minitest.h"
#include "nip.h"
#include "nip_vectors.h"

static void test_crc_check_value(void)
{
    const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    MT_CHECK_EQ(nip_crc16(check, sizeof check), 0x29B1);
}

static void test_valid_vectors_encode_and_decode(void)
{
    for (size_t i = 0; i < NIP_N_VALID; i++) {
        const nip_valid_vector_t *v = &NIP_VALID_VECTORS[i];
        nip_frame_t f = {.seq = v->seq,
                         .t_ms = v->t_ms,
                         .intent = v->intent,
                         .level = v->level,
                         .confidence = v->confidence,
                         .flags = v->flags};
        uint8_t out[NIP_FRAME_LEN];
        MT_CHECK_EQ(nip_encode(&f, out), NIP_OK);
        if (memcmp(out, v->bytes, NIP_FRAME_LEN) != 0) {
            fprintf(stderr, "encode mismatch: %s\n", v->name);
            MT_CHECK(0);
        }

        nip_frame_t d;
        MT_CHECK_EQ(nip_decode(v->bytes, NIP_FRAME_LEN, &d), NIP_OK);
        MT_CHECK_EQ(d.seq, v->seq);
        MT_CHECK_EQ(d.t_ms, v->t_ms);
        MT_CHECK_EQ(d.intent, v->intent);
        MT_CHECK_EQ(d.level, v->level);
        MT_CHECK_EQ(d.confidence, v->confidence);
        MT_CHECK_EQ(d.flags, v->flags);
    }
}

static void test_invalid_vectors_rejected_with_same_status(void)
{
    for (size_t i = 0; i < NIP_N_INVALID; i++) {
        const nip_invalid_vector_t *v = &NIP_INVALID_VECTORS[i];
        nip_frame_t d;
        nip_status_t st = nip_decode(v->bytes, v->len, &d);
        if ((int)st != v->status) {
            fprintf(stderr, "%s: got status %d, want %d\n", v->name, (int)st, v->status);
            MT_CHECK(0);
        }
    }
}

static void test_seq_vectors(void)
{
    for (size_t i = 0; i < NIP_N_SEQ; i++) {
        const nip_seq_vector_t *v = &NIP_SEQ_VECTORS[i];
        MT_CHECK_EQ(nip_seq_is_newer(v->candidate, v->last), v->newer);
    }
    MT_CHECK_EQ(nip_next_seq(255), 0);
    MT_CHECK_EQ(nip_next_seq(0), 1);
}

static void test_every_single_bit_flip_rejected(void)
{
    for (size_t i = 0; i < NIP_N_VALID; i++) {
        for (unsigned bit = 0; bit < NIP_FRAME_LEN * 8u; bit++) {
            uint8_t buf[NIP_FRAME_LEN];
            memcpy(buf, NIP_VALID_VECTORS[i].bytes, NIP_FRAME_LEN);
            buf[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
            nip_frame_t d;
            MT_CHECK(nip_decode(buf, NIP_FRAME_LEN, &d) != NIP_OK);
        }
    }
}

static void test_encoder_refuses_invalid_fields(void)
{
    uint8_t out[NIP_FRAME_LEN];
    nip_frame_t f = {.seq = 0, .t_ms = 0, .intent = 9, .level = 0, .confidence = 0, .flags = 0};
    MT_CHECK_EQ(nip_encode(&f, out), NIP_ERR_INTENT);
    f.intent = NIP_INTENT_FWD;
    f.level = 4;
    MT_CHECK_EQ(nip_encode(&f, out), NIP_ERR_LEVEL);
    f.level = 3;
    f.flags = 0x20;
    MT_CHECK_EQ(nip_encode(&f, out), NIP_ERR_FLAGS);
}

static void test_null_arguments(void)
{
    uint8_t buf[NIP_FRAME_LEN] = {0};
    nip_frame_t f = {0};
    MT_CHECK_EQ(nip_decode(NULL, NIP_FRAME_LEN, &f), NIP_ERR_ARG);
    MT_CHECK_EQ(nip_decode(buf, NIP_FRAME_LEN, NULL), NIP_ERR_ARG);
    MT_CHECK_EQ(nip_encode(NULL, buf), NIP_ERR_ARG);
    MT_CHECK_EQ(nip_encode(&f, NULL), NIP_ERR_ARG);
}

int main(void)
{
    MT_RUN(test_crc_check_value);
    MT_RUN(test_valid_vectors_encode_and_decode);
    MT_RUN(test_invalid_vectors_rejected_with_same_status);
    MT_RUN(test_seq_vectors);
    MT_RUN(test_every_single_bit_flip_rejected);
    MT_RUN(test_encoder_refuses_invalid_fields);
    MT_RUN(test_null_arguments);
    return MT_REPORT();
}
