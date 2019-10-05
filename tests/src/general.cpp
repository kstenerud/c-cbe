#include "helpers/test_helpers.h"

// #define KSLog_LocalMinLevel KSLOG_LEVEL_TRACE
#include <kslog/kslog.h>

using namespace encoding;

TEST_ENCODE_DECODE_SHRINKING(Bool, false, 0, b(false), {0x7c})
TEST_ENCODE_DECODE_SHRINKING(Bool, true,  0, b(true), {0x7d})

TEST_ENCODE_DECODE_SHRINKING(Int, p0,   0, i( 1,    0), {0x00})
TEST_ENCODE_DECODE_SHRINKING(Int, p1,   0, i( 1,    1), {0x01})
TEST_ENCODE_DECODE_SHRINKING(Int, p100, 0, i( 1,  100), {0x64})
TEST_ENCODE_DECODE_SHRINKING(Int, n1,   0, i(-1,    1), {0xff})
TEST_ENCODE_DECODE_SHRINKING(Int, n100, 0, i(-1,  100), {0x9c})
TEST_ENCODE_DECODE_SHRINKING(Int, p101, 0, i( 1,  101), {0x68, 0x65})
TEST_ENCODE_DECODE_SHRINKING(Int, n101, 0, i(-1,  101), {0x69, 0x65})
TEST_ENCODE_DECODE_SHRINKING(Int, px7f, 0, i( 1, 0x7f), {0x68, 0x7f})
TEST_ENCODE_DECODE_SHRINKING(Int, nx80, 0, i(-1, 0x80), {0x69, 0x80})
TEST_ENCODE_DECODE_SHRINKING(Int, pxff, 0, i( 1, 0xff), {0x68, 0xff})
TEST_ENCODE_DECODE_SHRINKING(Int, nxff, 0, i(-1, 0xff), {0x69, 0xff})

TEST_ENCODE_DECODE_SHRINKING(Int, px100, 0, i( 1,  0x100), {0x6a, 0x00, 0x01})
TEST_ENCODE_DECODE_SHRINKING(Int, nx100, 0, i(-1,  0x100), {0x6b, 0x00, 0x01})
TEST_ENCODE_DECODE_SHRINKING(Int, p7fff, 0, i( 1, 0x7fff), {0x6a, 0xff, 0x7f})
TEST_ENCODE_DECODE_SHRINKING(Int, n8000, 0, i(-1, 0x8000), {0x6b, 0x00, 0x80})
TEST_ENCODE_DECODE_SHRINKING(Int, pffff, 0, i( 1, 0xffff), {0x6a, 0xff, 0xff})
TEST_ENCODE_DECODE_SHRINKING(Int, nffff, 0, i(-1, 0xffff), {0x6b, 0xff, 0xff})

TEST_ENCODE_DECODE_SHRINKING(Int, p10000,    0, i( 1,     0x10000), {0x66, 0x84, 0x80, 0x00})
TEST_ENCODE_DECODE_SHRINKING(Int, n10000,    0, i(-1,     0x10000), {0x67, 0x84, 0x80, 0x00})
TEST_ENCODE_DECODE_SHRINKING(Int, p7fffffff, 0, i( 1,  0x7fffffff), {0x6c, 0xff, 0xff, 0xff, 0x7f})
TEST_ENCODE_DECODE_SHRINKING(Int, n80000000, 0, i(-1, 0x80000000L), {0x6d, 0x00, 0x00, 0x00, 0x80})
TEST_ENCODE_DECODE_SHRINKING(Int, pffffffff, 0, i( 1,  0xffffffff), {0x6c, 0xff, 0xff, 0xff, 0xff})
TEST_ENCODE_DECODE_SHRINKING(Int, nffffffff, 0, i(-1, 0xffffffffL), {0x6d, 0xff, 0xff, 0xff, 0xff})

TEST_ENCODE_DECODE_SHRINKING(Int, p100000000,        0, i( 1,        0x100000000L), {0x66, 0x90, 0x80, 0x80, 0x80, 0x00})
TEST_ENCODE_DECODE_SHRINKING(Int, n100000000,        0, i(-1,        0x100000000L), {0x67, 0x90, 0x80, 0x80, 0x80, 0x00})
TEST_ENCODE_DECODE_SHRINKING(Int, p7fffffffffffffff, 0, i( 1, 0x7fffffffffffffffL), {0x6e, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f})
TEST_ENCODE_DECODE_SHRINKING(Int, n8000000000000000, 0, i(-1, 0x8000000000000000L), {0x6f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80})
TEST_ENCODE_DECODE_SHRINKING(Int, pffffffffffffffff, 0, i( 1, 0xffffffffffffffffL), {0x6e, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff})

TEST_ENCODE_DECODE_SHRINKING(Float, p0_0,        0, f(0.0,         0), {0x70, 0x00, 0x00, 0x00, 0x00})
TEST_ENCODE_DECODE_SHRINKING(Float, n967234_125, 0, f(-967234.125, 0), {0x70, 0x22, 0x24, 0x6c, 0xc9})
TEST_ENCODE_DECODE_SHRINKING(Float, p1_0123,     0, f(1.0123,      0), {0x71, 0x51, 0xda, 0x1b, 0x7c, 0x61, 0x32, 0xf0, 0x3f})

TEST_ENCODE_DECODE_SHRINKING(Date, 2015_01_15, 0, d(2015, 1, 15), {0x99, 0x2f, 0x00, 0x1e})
TEST_ENCODE_DECODE_SHRINKING(Time, 23_14_43_1, 0, t(23, 14, 43, 1000000000), {0x9a, 0xbb, 0xce, 0x8a, 0x3e})
TEST_ENCODE_DECODE_SHRINKING(Time, 23_14_43_1_Berlin, 0, t(23, 14, 43, 1000000000, "E/Berlin"), {0x9a, 0xba, 0xce, 0x8a, 0x3e, 0x10, 'E', '/', 'B', 'e', 'r', 'l', 'i', 'n'})
TEST_ENCODE_DECODE_SHRINKING(Time, 23_14_43_1_loc, 0, t(23, 14, 43, 1000000000, 1402, 2099), {0x9a, 0xba, 0xce, 0x8a, 0x3e, 0xf5, 0x8a, 0x19, 0x04})
TEST_ENCODE_DECODE_SHRINKING(Timestamp, 1955_11_11_22_38_0_1, 0, ts(1955, 11, 11, 22, 38, 0, 1), {0x9b, 0x03, 0xa6, 0x5d, 0x1b, 0x00, 0x00, 0x00, 0x04, 0x33})
TEST_ENCODE_DECODE_SHRINKING(Timestamp, 1985_10_26_01_22_16_LA, 0, ts(1985, 10, 26, 1, 22, 16, 0, "M/Los_Angeles"), {0x9b, 0x40, 0x56, 0xd0, 0x0a, 0x3a, 0x1a, 'M', '/', 'L', 'o', 's', '_', 'A', 'n', 'g', 'e', 'l', 'e', 's'})
TEST_ENCODE_DECODE_SHRINKING(Timestamp, 2015_10_21_07_28_00_loc, 0, ts(2015, 10, 21, 7, 28, 0, 0, 3399, 11793), {0x9b, 0x00, 0xdc, 0xa9, 0x0a, 0x3c, 0x8f, 0x9a, 0x08, 0x17})

TEST_ENCODE_DECODE_DATA_ENCODING(DecFloat, 0_1, 99, 9, df(0.1dd, 0), df(0.1, 0), {0x65, 0x06, 0x01})
TEST_ENCODE_DECODE_DATA_ENCODING(DecFloat, 0_194_2, 99, 9, df(0.194dd, 2), df(0.19, 0), {0x65, 0x0a, 0x13})
TEST_ENCODE_DECODE_DATA_ENCODING(DecFloat, 19_4659234442e100_9, 99, 9, df(19.4659234442e100, 9), df(1.94659234e101, 0), {0x65, 0x82, 0x74, 0xdc, 0xe9, 0x87, 0x22})

TEST_ENCODE_DECODE_SHRINKING(Nil, nil, 0, nil(), {0x7e})

TEST_ENCODE_DATA(Padding, pad_1, 99, 9, pad(1), {0x7f})
TEST_ENCODE_DATA(Padding, pad_2, 99, 9, pad(2), {0x7f, 0x7f})

TEST_STOP_IN_CALLBACK(SIC, Nil, nil())
TEST_STOP_IN_CALLBACK(SIC, Bool, b(false))
TEST_STOP_IN_CALLBACK(SIC, Int, i(1))
TEST_STOP_IN_CALLBACK(SIC, Float, f(1, 0))
TEST_STOP_IN_CALLBACK(SIC, Decimal, df(1, 1))
TEST_STOP_IN_CALLBACK(SIC, Date, d(1, 1, 1))
TEST_STOP_IN_CALLBACK(SIC, Time, t(1, 1, 1, 1))
TEST_STOP_IN_CALLBACK(SIC, Time_zone, t(1, 1, 1, 1, "E/Berlin"))
TEST_STOP_IN_CALLBACK(SIC, Time_loc, t(1, 1, 1, 1, 0, 0))
TEST_STOP_IN_CALLBACK(SIC, Timestamp, ts(1, 1, 1, 1, 1, 1, 1))
TEST_STOP_IN_CALLBACK(SIC, Timestamp_zone, ts(1, 1, 1, 1, 1, 1, 1, "E/Berlin"))
TEST_STOP_IN_CALLBACK(SIC, Timestamp_loc, ts(1, 1, 1, 1, 1, 1, 1, 0, 0))
TEST_STOP_IN_CALLBACK(SIC, List, list().end())
TEST_STOP_IN_CALLBACK(SIC, Map, umap().end())
TEST_STOP_IN_CALLBACK(SIC, String, str("test"))
TEST_STOP_IN_CALLBACK(SIC, Bytes, bin(std::vector<uint8_t>()))
