#ifndef cbe_internal_H
#define cbe_internal_H

#include "cbe/cbe.h"

typedef enum
{
    TYPE_SMALLINT_MIN      = -100,
    TYPE_SMALLINT_MAX      =  100,
    TYPE_FLOAT_DECIMAL     = 0x65,
    TYPE_INT_POS           = 0x66,
    TYPE_INT_NEG           = 0x67,
    TYPE_INT_POS_8         = 0x68,
    TYPE_INT_NEG_8         = 0x69,
    TYPE_INT_POS_16        = 0x6a,
    TYPE_INT_NEG_16        = 0x6b,
    TYPE_INT_POS_32        = 0x6c,
    TYPE_INT_NEG_32        = 0x6d,
    TYPE_INT_POS_64        = 0x6e,
    TYPE_INT_NEG_64        = 0x6f,
    TYPE_FLOAT_BINARY_32   = 0x70,
    TYPE_FLOAT_BINARY_64   = 0x71,
    // RESERVED 0x72 - 0x76
    TYPE_LIST              = 0x77,
    TYPE_MAP_UNORDERED     = 0x78,
    TYPE_MAP_ORDERED       = 0x79,
    TYPE_MAP_METADATA      = 0x7a,
    TYPE_END_CONTAINER     = 0x7b,
    TYPE_FALSE             = 0x7c,
    TYPE_TRUE              = 0x7d,
    TYPE_NIL               = 0x7e,
    TYPE_PADDING           = 0x7f,
    TYPE_STRING_0          = 0x80,
    TYPE_STRING_1          = 0x81,
    TYPE_STRING_2          = 0x82,
    TYPE_STRING_3          = 0x83,
    TYPE_STRING_4          = 0x84,
    TYPE_STRING_5          = 0x85,
    TYPE_STRING_6          = 0x86,
    TYPE_STRING_7          = 0x87,
    TYPE_STRING_8          = 0x88,
    TYPE_STRING_9          = 0x89,
    TYPE_STRING_10         = 0x8a,
    TYPE_STRING_11         = 0x8b,
    TYPE_STRING_12         = 0x8c,
    TYPE_STRING_13         = 0x8d,
    TYPE_STRING_14         = 0x8e,
    TYPE_STRING_15         = 0x8f,
    TYPE_STRING            = 0x90,
    TYPE_BYTES             = 0x91,
    TYPE_URI               = 0x92,
    TYPE_COMMENT           = 0x93,
    // RESERVED 0x94 - 0x98
    TYPE_DATE              = 0x99,
    TYPE_TIME              = 0x9a,
    TYPE_TIMESTAMP         = 0x9b,
} cbe_type_field;

typedef enum
{
    ARRAY_TYPE_STRING,
    ARRAY_TYPE_BYTES,
    ARRAY_TYPE_URI,
    ARRAY_TYPE_COMMENT,
} array_type;


static inline int get_max_container_depth_or_default(int max_container_depth)
{
    return max_container_depth > 0 ? max_container_depth : CBE_DEFAULT_MAX_CONTAINER_DEPTH;
}

static inline void zero_memory(void* const memory, const int byte_count)
{
    uint8_t* ptr = memory;
    uint8_t* const end = ptr + byte_count;
    while(ptr < end)
    {
        *ptr++ = 0;
    }
}

bool cbe_validate_string(const uint8_t* const start, const int64_t byte_count);

bool cbe_validate_uri(const uint8_t* const start, const int64_t byte_count);

bool cbe_validate_comment(const uint8_t* const start, const int64_t byte_count);

#endif // cbe_internal_H
