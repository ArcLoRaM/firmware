#include "unity.h"
#include "node_id.h"

void setUp(void)    {}
void tearDown(void) {}

static const NodeIdEntry_t k_two[] = {
    { { 0x00200041u, 0x5642500Au, 0x20383353u }, 1u },
    { { 0x00200041u, 0x5642500Au, 0x20383354u }, 2u },
};

/* --- Lookup -------------------------------------------------------------- */

void test_find_returns_the_registered_node_id(void)
{
    const uint32_t uid[NODE_UID_WORDS] = { 0x00200041u, 0x5642500Au, 0x20383354u };
    TEST_ASSERT_EQUAL_UINT8(2u, NodeId_Find(k_two, 2u, uid));
}

void test_find_matches_all_three_words(void)
{
    /* Boards of one wafer lot share w0/w1: w2 alone must not match. */
    const uint32_t uid[NODE_UID_WORDS] = { 0x00200042u, 0x5642500Au, 0x20383353u };
    TEST_ASSERT_EQUAL_UINT8(NODE_ID_UNPROVISIONED, NodeId_Find(k_two, 2u, uid));
}

void test_unknown_uid_is_unprovisioned(void)
{
    const uint32_t uid[NODE_UID_WORDS] = { 1u, 2u, 3u };
    TEST_ASSERT_EQUAL_UINT8(NODE_ID_UNPROVISIONED, NodeId_FromUid(uid));
}

void test_end_marker_is_not_a_board(void)
{
    const uint32_t zero[NODE_UID_WORDS] = { 0u, 0u, 0u };
    TEST_ASSERT_EQUAL_UINT8(NODE_ID_UNPROVISIONED, NodeId_FromUid(zero));
}

/* --- Registered table ---------------------------------------------------- */

void test_registered_ids_are_assignable(void)
{
    size_t n;
    const NodeIdEntry_t *t = NodeId_Table(&n);
    for (size_t i = 0u; i < n; i++) {
        TEST_ASSERT_NOT_EQUAL_MESSAGE(NODE_ID_UNPROVISIONED, t[i].node_id, "0x00 is reserved");
        TEST_ASSERT_NOT_EQUAL_MESSAGE(NODE_ID_BROADCAST, t[i].node_id, "0xFF is reserved");
    }
}

void test_registered_ids_and_uids_are_unique(void)
{
    size_t n;
    const NodeIdEntry_t *t = NodeId_Table(&n);
    for (size_t i = 0u; i < n; i++) {
        for (size_t j = i + 1u; j < n; j++) {
            TEST_ASSERT_NOT_EQUAL_MESSAGE(t[i].node_id, t[j].node_id, "duplicate node ID");
            TEST_ASSERT_FALSE_MESSAGE(t[i].uid[0] == t[j].uid[0] &&
                                      t[i].uid[1] == t[j].uid[1] &&
                                      t[i].uid[2] == t[j].uid[2], "duplicate UID");
        }
    }
}

void test_every_registered_board_finds_itself(void)
{
    size_t n;
    const NodeIdEntry_t *t = NodeId_Table(&n);
    for (size_t i = 0u; i < n; i++) {
        TEST_ASSERT_EQUAL_UINT8(t[i].node_id, NodeId_FromUid(t[i].uid));
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_find_returns_the_registered_node_id);
    RUN_TEST(test_find_matches_all_three_words);
    RUN_TEST(test_unknown_uid_is_unprovisioned);
    RUN_TEST(test_end_marker_is_not_a_board);
    RUN_TEST(test_registered_ids_are_assignable);
    RUN_TEST(test_registered_ids_and_uids_are_unique);
    RUN_TEST(test_every_registered_board_finds_itself);
    return UNITY_END();
}
