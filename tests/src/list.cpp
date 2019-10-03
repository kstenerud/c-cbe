#include "helpers/test_helpers.h"

// #define KSLog_LocalMinLevel KSLOG_LEVEL_TRACE
#include <kslog/kslog.h>

using namespace encoding;

TEST_ENCODE_DECODE_SHRINKING(List, size_0, 1, list().end(), {0x77, 0x7b})
TEST_ENCODE_DECODE_SHRINKING(List, size_1, 1, list().i(1).end(), {0x77, 0x01, 0x7b})
TEST_ENCODE_DECODE_SHRINKING(List, size_2, 1, list().str("1").i(1).end(), {0x77, 0x81, 0x31, 0x01, 0x7b})

TEST_ENCODE_DECODE_STATUS(List, unterminated,   99, 9, CBE_ENCODE_ERROR_UNBALANCED_CONTAINERS, CBE_DECODE_ERROR_UNBALANCED_CONTAINERS, list())
TEST_ENCODE_DECODE_STATUS(List, unterminated_2, 99, 9, CBE_ENCODE_ERROR_UNBALANCED_CONTAINERS, CBE_DECODE_ERROR_UNBALANCED_CONTAINERS, list().f(0.1, 0))
TEST_ENCODE_DECODE_STATUS(List, unterminated_3, 99, 9, CBE_ENCODE_ERROR_UNBALANCED_CONTAINERS, CBE_DECODE_ERROR_UNBALANCED_CONTAINERS, list().umap())

// Can't test decode because ending the container ends the document.
TEST_ENCODE_STATUS(List, encode_extra_end,   99, 9, CBE_ENCODE_ERROR_UNBALANCED_CONTAINERS, list().end().end())
TEST_ENCODE_STATUS(List, encode_extra_end_2, 99, 9, CBE_ENCODE_ERROR_UNBALANCED_CONTAINERS, list().umap().end().end().end())

TEST_ENCODE_STATUS(List, too_deep, 99, 3, CBE_ENCODE_ERROR_MAX_CONTAINER_DEPTH_EXCEEDED, list().list().list().list().end().end().end().end())
