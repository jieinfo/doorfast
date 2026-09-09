#include <limits.h>
#include <string.h>

#include "gvs_reply_queue.h"
#include "test.h"

static struct df_gvs_peer_reply make_reply(uint8_t extension, uint8_t token) {
    struct df_gvs_peer_reply reply = {
        .target = {0x61, 2, 1, 1, 1, extension},
        .request_data = {token, (uint8_t)(token + 1U)},
        .peer_observed = true,
    };

    return reply;
}

void test_gvs_reply_queue_coalesces_and_expires_pending_replies(void) {
    struct df_gvs_reply_queue queue, before;
    struct df_gvs_reply_queue_entry entry;
    struct df_gvs_peer_reply first = make_reply(1, 0x10);
    struct df_gvs_peer_reply second = make_reply(2, 0x20);
    bool coalesced = true;
    size_t expired = 99;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_reply_queue_init(&queue, 100));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_reply_queue_enqueue(
                                      &queue, &first, 100, &coalesced));
    TEST_ASSERT_INT_EQ(0, coalesced);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_reply_queue_enqueue(
                                      &queue, &second, 101, &coalesced));
    TEST_ASSERT_INT_EQ(0, coalesced);
    TEST_ASSERT_INT_EQ(2, (int)df_gvs_reply_queue_count(&queue));

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_reply_queue_enqueue(
                                      &queue, &first, 200, &coalesced));
    TEST_ASSERT_INT_EQ(1, coalesced);
    TEST_ASSERT_INT_EQ(2, (int)df_gvs_reply_queue_count(&queue));
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_reply_queue_expire(&queue, 1102, &expired));
    TEST_ASSERT_INT_EQ(1, (int)expired);
    TEST_ASSERT_INT_EQ(1, (int)df_gvs_reply_queue_count(&queue));
    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_reply_queue_take(&queue, 1102, &entry));
    TEST_ASSERT_INT_EQ(0, memcmp(&first, &entry.reply, sizeof(first)));
    TEST_ASSERT_INT_EQ(2, (int)entry.repeat_count);
    TEST_ASSERT_INT_EQ(0, (int)df_gvs_reply_queue_count(&queue));

    before = queue;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
                       df_gvs_reply_queue_expire(&queue, 1101, &expired));
    TEST_ASSERT_INT_EQ(0, memcmp(&before, &queue, sizeof(queue)));
}

void test_gvs_reply_queue_is_bounded_and_failure_atomic(void) {
    struct df_gvs_reply_queue queue, before;
    struct df_gvs_peer_reply reply;
    bool coalesced;
    size_t expired;
    unsigned index;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_reply_queue_init(&queue, 0));
    for (index = 0; index < DF_GVS_REPLY_QUEUE_CAPACITY; index++) {
        reply = make_reply((uint8_t)(index + 1U), (uint8_t)index);
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_reply_queue_enqueue(
                                          &queue, &reply, index, &coalesced));
    }
    before = queue;
    reply = make_reply(99, 99);
    TEST_ASSERT_INT_EQ(DF_ERR_IO, df_gvs_reply_queue_enqueue(
                                         &queue, &reply, 20, &coalesced));
    TEST_ASSERT_INT_EQ(0, memcmp(&before, &queue, sizeof(queue)));
    TEST_ASSERT_INT_EQ(DF_GVS_REPLY_QUEUE_CAPACITY,
                       (int)df_gvs_reply_queue_count(&queue));

    TEST_ASSERT_INT_EQ(DF_OK,
                       df_gvs_reply_queue_expire(&queue, 1016, &expired));
    TEST_ASSERT_INT_EQ(DF_GVS_REPLY_QUEUE_CAPACITY, (int)expired);
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_reply_queue_enqueue(
                                      &queue, &reply, 1016, &coalesced));
    before = queue;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_reply_queue_enqueue(
                                              &queue, &reply,
                                              UINT64_MAX, &coalesced));
    TEST_ASSERT_INT_EQ(0, memcmp(&before, &queue, sizeof(queue)));
}
