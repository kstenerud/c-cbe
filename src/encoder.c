#include "cbe_internal.h"
#include <compact_float/compact_float.h>
#include <compact_time/compact_time.h>
#include <endianness/endianness.h>
#include <vlq/vlq.h>

// #define KSLog_LocalMinLevel KSLOG_LEVEL_TRACE
#include <kslog/kslog.h>


// ====
// Data
// ====

struct cbe_encode_process
{
    struct
    {
        const uint8_t* start;
        const uint8_t* end;
        uint8_t* position;
    } buffer;
    struct
    {
        bool is_inside_array;
        array_type type;
        int64_t current_offset;
        int64_t byte_count;
    } array;
    struct
    {
        int max_depth;
        int level;
        bool next_object_is_map_key;
    } container;
    bool is_inside_map[];
};
typedef struct cbe_encode_process cbe_encode_process;

typedef uint8_t cbe_encoded_type_field;


// ==============
// Utility Macros
// ==============

#define likely_if(TEST_FOR_TRUTH) if(__builtin_expect(TEST_FOR_TRUTH, 1))
#define unlikely_if(TEST_FOR_TRUTH) if(__builtin_expect(TEST_FOR_TRUTH, 0))

#define MASK_BITS(BIT_COUNT) ((1UL << BIT_COUNT) - 1)
#define RSHIFT_MAX(VALUE) ( (VALUE) >> (sizeof(VALUE)*8 - 1) )


// ==============
// Error Handlers
// ==============

#define STOP_AND_EXIT_IF_NOT_ENOUGH_ROOM(PROCESS, REQUIRED_BYTES) \
    unlikely_if(buff_remaining_length(PROCESS) < (int64_t)(REQUIRED_BYTES)) \
    { \
        KSLOG_DEBUG("STOP AND EXIT: Require %d bytes but only %d available.", \
            (REQUIRED_BYTES), buff_remaining_length(PROCESS)); \
        return CBE_ENCODE_STATUS_NEED_MORE_ROOM; \
    }

#define STOP_AND_EXIT_IF_NOT_ENOUGH_ROOM_WITH_TYPE(PROCESS, REQUIRED_BYTES) \
    STOP_AND_EXIT_IF_NOT_ENOUGH_ROOM(PROCESS, ((REQUIRED_BYTES) + sizeof(cbe_encoded_type_field)))

#define STOP_AND_EXIT_IF_IS_INSIDE_ARRAY(PROCESS) \
    unlikely_if((PROCESS)->array.is_inside_array) \
    { \
        KSLOG_DEBUG("STOP AND EXIT: We're inside an array when we shouldn't be"); \
        return CBE_ENCODE_ERROR_INCOMPLETE_ARRAY_FIELD; \
    }

#define STOP_AND_EXIT_IF_IS_NOT_INSIDE_ARRAY(PROCESS) \
    unlikely_if(!(PROCESS)->array.is_inside_array) \
    { \
        KSLOG_DEBUG("STOP AND EXIT: We're not inside an array when we should be"); \
        return CBE_ENCODE_ERROR_NOT_INSIDE_ARRAY_FIELD; \
    }

#define STOP_AND_EXIT_IF_ARRAY_LENGTH_EXCEEDED(PROCESS, BYTE_COUNT) \
{ \
    int64_t new_array_offset = (PROCESS)->array.current_offset + (BYTE_COUNT); \
    unlikely_if(new_array_offset > (PROCESS)->array.byte_count) \
    { \
        KSLOG_DEBUG("STOP AND EXIT: Attempted to write %d bytes to array with only %d availale", \
            new_array_offset, (PROCESS)->array.byte_count); \
        return CBE_ENCODE_ERROR_ARRAY_FIELD_LENGTH_EXCEEDED; \
    } \
}

#define STOP_AND_EXIT_IF_MAX_CONTAINER_DEPTH_EXCEEDED(PROCESS) \
    unlikely_if((PROCESS)->container.level + 1 >= (PROCESS)->container.max_depth) \
    { \
        KSLOG_DEBUG("STOP AND EXIT: Max depth %d exceeded", (PROCESS)->container.max_depth); \
        return CBE_ENCODE_ERROR_MAX_CONTAINER_DEPTH_EXCEEDED; \
    }

#define STOP_AND_EXIT_IF_IS_NOT_INSIDE_CONTAINER(PROCESS) \
    unlikely_if((PROCESS)->container.level <= 0) \
    { \
        KSLOG_DEBUG("STOP AND EXIT: We're not inside a container"); \
        return CBE_ENCODE_ERROR_UNBALANCED_CONTAINERS; \
    }

#define STOP_AND_EXIT_IF_IS_INSIDE_CONTAINER(PROCESS) \
    unlikely_if((PROCESS)->container.level != 0) \
    { \
        KSLOG_DEBUG("STOP AND EXIT: There are still open containers when there shouldn't be"); \
        return CBE_ENCODE_ERROR_UNBALANCED_CONTAINERS; \
    }

#define STOP_AND_EXIT_IF_MAP_VALUE_MISSING(PROCESS) \
    unlikely_if((PROCESS)->is_inside_map[(PROCESS)->container.level] && \
        !(PROCESS)->container.next_object_is_map_key) \
    { \
        KSLOG_DEBUG("STOP AND EXIT: No map value provided for previous key"); \
        return CBE_ENCODE_ERROR_MAP_MISSING_VALUE_FOR_KEY; \
    }

#define STOP_AND_EXIT_IF_IS_WRONG_MAP_KEY_TYPE(PROCESS) \
    unlikely_if((PROCESS)->is_inside_map[process->container.level] && \
        (PROCESS)->container.next_object_is_map_key) \
    { \
        KSLOG_DEBUG("STOP AND EXIT: Map key has an invalid type"); \
        return CBE_ENCODE_ERROR_INCORRECT_MAP_KEY_TYPE; \
    }


// =======
// Utility
// =======

#define FITS_IN_INT_SMALL(VALUE)  ((VALUE) <= TYPE_SMALLINT_MAX)
#define FITS_IN_UINT_8(VALUE)     ((VALUE) == (uint8_t)(VALUE))
#define FITS_IN_UINT_16(VALUE)    ((VALUE) == (uint16_t)(VALUE))
#define FITS_IN_UINT_21(VALUE)    ((VALUE) == ((VALUE) & MASK_BITS(21)))
#define FITS_IN_UINT_32(VALUE)    ((VALUE) == (uint32_t)(VALUE))
#define FITS_IN_UINT_49(VALUE)    ((VALUE) == ((VALUE) & MASK_BITS(49)))
#define FITS_IN_UINT_64(VALUE)    ((VALUE) == (uint64_t)(VALUE))
#define FITS_IN_FLOAT_32(VALUE)   ((VALUE) == (double)(float)(VALUE))
#define FITS_IN_FLOAT_64(VALUE)   ((VALUE) == (double)(VALUE))
#define FITS_IN_DECIMAL_32(VALUE) ((VALUE) == (dec32_ct)(VALUE))
#define FITS_IN_DECIMAL_64(VALUE) ((VALUE) == (dec64_ct)(VALUE))

static inline int64_t minimum_int64(const int64_t a, const int64_t b)
{
    return a < b ? a : b;
}

static inline int64_t buff_remaining_length(cbe_encode_process* const process)
{
    return process->buffer.end - process->buffer.position;
}

static inline void swap_map_key_value_status(cbe_encode_process* const process)
{
    process->container.next_object_is_map_key = !process->container.next_object_is_map_key;
}

static inline int get_array_length_field_width(const int64_t length)
{
    uint64_t ulength = ((uint64_t)length) & 0x7fffffffffffffffULL;
    if(ulength <= 0x7f) {
        return 1;
    }

    int size = 0;
    while(ulength > 0) {
        ulength >>= 7;
        size++;
    }
    return size;
}

static inline void add_primitive_type(cbe_encode_process* const process, const cbe_type_field type)
{
    KSLOG_DEBUG("[%02x]", type);

    *process->buffer.position++ = (uint8_t)type;
}
static inline void add_primitive_uint8(cbe_encode_process* const process, const uint8_t value)
{
    KSLOG_DEBUG("[%02x]", value);

    *process->buffer.position++ = value;
}
static inline void add_primitive_int8(cbe_encode_process* const process, const int8_t value)
{
    KSLOG_DEBUG("[%02x] (%d)", value & 0xff, value);

    *process->buffer.position++ = (uint8_t)value;
}
#define DEFINE_PRIMITIVE_ADD_FUNCTION(DATA_TYPE, DEFINITION_TYPE) \
    static inline void add_primitive_ ## DEFINITION_TYPE(cbe_encode_process* const process, const DATA_TYPE value) \
    { \
        write_##DEFINITION_TYPE##_le(value, process->buffer.position); \
        KSLOG_DATA_DEBUG(process->buffer.position, sizeof(value), NULL); \
        process->buffer.position += sizeof(value); \
    }
DEFINE_PRIMITIVE_ADD_FUNCTION(uint16_t,    uint16)
DEFINE_PRIMITIVE_ADD_FUNCTION(uint32_t,    uint32)
DEFINE_PRIMITIVE_ADD_FUNCTION(uint64_t,    uint64)
DEFINE_PRIMITIVE_ADD_FUNCTION(float,       float32)
DEFINE_PRIMITIVE_ADD_FUNCTION(double,      float64)

static inline void add_primitive_rvlq(cbe_encode_process* const process, uint64_t value)
{
        int byte_count = rvlq_encode_64(value, process->buffer.position, buff_remaining_length(process));
        KSLOG_DATA_DEBUG(process->buffer.position, byte_count, NULL);
        process->buffer.position += byte_count;
}

static inline void add_primitive_bytes(cbe_encode_process* const process,
                                       const uint8_t* const bytes,
                                       const int64_t byte_count)
{
    if(byte_count < 50)
    {
        KSLOG_DATA_DEBUG(bytes, byte_count, "%d Bytes: ", byte_count);
    }
    else
    {
        KSLOG_DATA_TRACE(bytes, byte_count, "%d Bytes: ", byte_count);
    }

    memcpy(process->buffer.position, bytes, byte_count);
    process->buffer.position += byte_count;
}

static inline void begin_array(cbe_encode_process* const process, array_type type, int64_t byte_count)
{
    KSLOG_DEBUG("(process %p, type %d, byte_count %d)", process, type, byte_count);
    process->array.is_inside_array = true;
    process->array.current_offset = 0;
    process->array.type = type;
    process->array.byte_count = byte_count;
}

static inline void end_array(cbe_encode_process* const process)
{
    KSLOG_DEBUG("(process %p)", process);
    process->array.is_inside_array = false;
    process->array.current_offset = 0;
    process->array.byte_count = 0;
}

static inline void add_array_length_field(cbe_encode_process* const process, const int64_t length)
{
    KSLOG_DEBUG("(process %p, length %d)", process, length);
    uint64_t ulength = ((uint64_t)length) & 0x7fffffffffffffffULL;

    int byteCount = get_array_length_field_width(length);
    for(int i = 0; i < byteCount; i++) {
        uint8_t nextByte = (uint8_t)((ulength >> (7*(byteCount-i-1))) & 0x7f);
        if(i != byteCount - 1) {
            nextByte |= 0x80;
        }
        add_primitive_uint8(process, nextByte);
    }
}

static inline cbe_encode_status add_int_small(cbe_encode_process* const process, const int8_t value)
{
    KSLOG_DEBUG("(process %p, value %d)", process, value);

    STOP_AND_EXIT_IF_IS_INSIDE_ARRAY(process);
    STOP_AND_EXIT_IF_NOT_ENOUGH_ROOM_WITH_TYPE(process, 0);

    add_primitive_int8(process, value);
    swap_map_key_value_status(process);

    return CBE_ENCODE_STATUS_OK;
}

#define DEFINE_ADD_SCALAR_FUNCTION(DATA_TYPE, NAME, DEFINITION_TYPE, CBE_TYPE) \
    static inline cbe_encode_status add_ ## NAME(cbe_encode_process* const process, const DATA_TYPE value) \
    { \
        KSLOG_DEBUG("(process %p)", process); \
    \
        STOP_AND_EXIT_IF_IS_INSIDE_ARRAY(process); \
        STOP_AND_EXIT_IF_NOT_ENOUGH_ROOM_WITH_TYPE(process, sizeof(value)); \
    \
        add_primitive_type(process, CBE_TYPE); \
        add_primitive_ ## DEFINITION_TYPE(process, value); \
    \
        swap_map_key_value_status(process); \
    \
        return CBE_ENCODE_STATUS_OK; \
    }
DEFINE_ADD_SCALAR_FUNCTION(float,  float_32, float32, TYPE_FLOAT_BINARY_32)
DEFINE_ADD_SCALAR_FUNCTION(double, float_64, float64, TYPE_FLOAT_BINARY_64)

ANSI_EXTENSION static inline cbe_encode_status add_float_decimal(cbe_encode_process* const process, _Decimal64 value, int significant_digits)
{
    KSLOG_DEBUG("(process %p)", process);

    STOP_AND_EXIT_IF_IS_INSIDE_ARRAY(process);
    STOP_AND_EXIT_IF_NOT_ENOUGH_ROOM_WITH_TYPE(process, 1);

    uint8_t* old_position = process->buffer.position;

    add_primitive_type(process, TYPE_FLOAT_DECIMAL);
    int bytes_encoded = cfloat_encode(value, significant_digits, process->buffer.position, buff_remaining_length(process));
    if(bytes_encoded < 1)
    {
        KSLOG_DEBUG("Not enough room to encode decimal ~ %f with %d significant digits", (double)value, significant_digits);
        process->buffer.position = old_position;
        return CBE_ENCODE_STATUS_NEED_MORE_ROOM;
    }
    process->buffer.position += bytes_encoded;

    swap_map_key_value_status(process);

    return CBE_ENCODE_STATUS_OK;
}

#define DEFINE_ADD_INT_FUNCTION(DATA_TYPE, NAME, DEFINITION_TYPE, CBE_TYPE) \
    static inline cbe_encode_status add_ ## NAME(cbe_encode_process* const process, const int is_negative, const DATA_TYPE value) \
    { \
        KSLOG_DEBUG("(process %p)", process); \
    \
        STOP_AND_EXIT_IF_IS_INSIDE_ARRAY(process); \
        STOP_AND_EXIT_IF_NOT_ENOUGH_ROOM_WITH_TYPE(process, sizeof(value)); \
    \
        add_primitive_type(process, CBE_TYPE + is_negative); \
        add_primitive_ ## DEFINITION_TYPE(process, value); \
    \
        swap_map_key_value_status(process); \
    \
        return CBE_ENCODE_STATUS_OK; \
    }
DEFINE_ADD_INT_FUNCTION(uint8_t,  int_8,  uint8,  TYPE_INT_POS_8)
DEFINE_ADD_INT_FUNCTION(uint16_t, int_16, uint16, TYPE_INT_POS_16)
DEFINE_ADD_INT_FUNCTION(uint32_t, int_32, uint32, TYPE_INT_POS_32)
DEFINE_ADD_INT_FUNCTION(uint64_t, int_64, uint64, TYPE_INT_POS_64)

static inline cbe_encode_status add_int(cbe_encode_process* const process, const int is_negative, const uint64_t value)
{
    KSLOG_DEBUG("(process %p)", process);

    STOP_AND_EXIT_IF_IS_INSIDE_ARRAY(process);
    STOP_AND_EXIT_IF_NOT_ENOUGH_ROOM_WITH_TYPE(process, rvlq_encoded_size_64(value));

    add_primitive_type(process, TYPE_INT_POS + is_negative);
    add_primitive_rvlq(process, value);

    swap_map_key_value_status(process);

    return CBE_ENCODE_STATUS_OK;
}

static inline cbe_encode_status encode_string_header(cbe_encode_process* const process,
                                                     const int64_t byte_count,
                                                     const bool should_reserve_payload)
{
    KSLOG_DEBUG("(process %p, byte_count %d, should_reserve_payload %d)",
        process, byte_count, should_reserve_payload);

    cbe_type_field type = 0;
    int reserved_count = 0;

    if(byte_count > 15)
    {
        type = TYPE_STRING;
        reserved_count = get_array_length_field_width(byte_count);
    }
    else
    {
        type = TYPE_STRING_0 + byte_count;
    }

    if(should_reserve_payload)
    {
        reserved_count += byte_count;
    }

    STOP_AND_EXIT_IF_NOT_ENOUGH_ROOM_WITH_TYPE(process, reserved_count);

    add_primitive_type(process, type);
    if(byte_count > 15)
    {
        add_array_length_field(process, byte_count);
    }
    begin_array(process, ARRAY_TYPE_STRING, byte_count);

    swap_map_key_value_status(process);
    unlikely_if(byte_count == 0)
    {
        end_array(process);
    }

    return CBE_ENCODE_STATUS_OK;
}

static cbe_encode_status encode_array_contents(cbe_encode_process* const process, 
                                               const uint8_t* const start,
                                               int64_t* const byte_count)
{
    KSLOG_DEBUG("(process %p, start %p, byte_count %d)", process, start, *byte_count);
    STOP_AND_EXIT_IF_NOT_ENOUGH_ROOM(process, 0);

    likely_if(*byte_count > 0)
    {
        const int64_t want_to_copy = *byte_count;
        const int64_t space_in_buffer = buff_remaining_length(process);
        const int64_t bytes_to_copy = minimum_int64(want_to_copy, space_in_buffer);

        KSLOG_DEBUG("Type: %d", process->array.type);
        switch(process->array.type)
        {
            case ARRAY_TYPE_STRING:
                unlikely_if(!cbe_validate_string(start, bytes_to_copy))
                {
                    KSLOG_DEBUG("invalid data");
                    return CBE_ENCODE_ERROR_INVALID_ARRAY_DATA;
                }
                break;
            case ARRAY_TYPE_URI:
                unlikely_if(!cbe_validate_uri(start, bytes_to_copy))
                {
                    KSLOG_DEBUG("invalid data");
                    return CBE_ENCODE_ERROR_INVALID_ARRAY_DATA;
                }
                break;
            case ARRAY_TYPE_COMMENT:
                unlikely_if(!cbe_validate_comment(start, bytes_to_copy))
                {
                    KSLOG_DEBUG("invalid data");
                    return CBE_ENCODE_ERROR_INVALID_ARRAY_DATA;
                }
                break;
            case ARRAY_TYPE_BYTES:
                // Nothing to do
                break;
        }

        add_primitive_bytes(process, start, bytes_to_copy);
        process->array.current_offset += bytes_to_copy;
        *byte_count = bytes_to_copy;

        KSLOG_DEBUG("Streamed %d bytes into array", bytes_to_copy);
        STOP_AND_EXIT_IF_NOT_ENOUGH_ROOM(process, want_to_copy - bytes_to_copy);
    }

    if(process->array.current_offset == process->array.byte_count)
    {
        KSLOG_DEBUG("Array has ended");
        end_array(process);
    }

    return CBE_ENCODE_STATUS_OK;
}

// ===
// API
// ===

int cbe_encode_process_size(const int max_container_depth)
{
    KSLOG_TRACE("(max_container_depth %d)", max_container_depth);
    return sizeof(cbe_encode_process) + get_max_container_depth_or_default(max_container_depth);
}


cbe_encode_status cbe_encode_begin(struct cbe_encode_process* const process,
                                   uint8_t* const document_buffer,
                                   const int64_t byte_count,
                                   const int max_container_depth)
{
    KSLOG_TRACE("(process %p, document_buffer %p, byte_count %d, max_container_depth %d)",
        process, document_buffer, byte_count, max_container_depth);
    unlikely_if(process == NULL || document_buffer == NULL || byte_count < 0)
    {
        return CBE_ENCODE_ERROR_INVALID_ARGUMENT;
    }

    zero_memory(process, sizeof(*process) + 1);
    cbe_encode_status status = cbe_encode_set_buffer(process, document_buffer, byte_count);
    unlikely_if(status != CBE_ENCODE_STATUS_OK)
    {
        return status;
    }
    process->container.max_depth = get_max_container_depth_or_default(max_container_depth);

    return status;
}

cbe_encode_status cbe_encode_set_buffer(cbe_encode_process* const process,
                                        uint8_t* const document_buffer,
                                        const int64_t byte_count)
{
    KSLOG_DEBUG("(process %p, document_buffer %p, byte_count %d)",
        process, document_buffer, byte_count);
    unlikely_if(process == NULL || document_buffer == NULL || byte_count < 0)
    {
        return CBE_ENCODE_ERROR_INVALID_ARGUMENT;
    }

    process->buffer.start = document_buffer;
    process->buffer.position = document_buffer;
    process->buffer.end = document_buffer + byte_count;

    return CBE_ENCODE_STATUS_OK;
}

int64_t cbe_encode_get_buffer_offset(cbe_encode_process* const process)
{
    KSLOG_DEBUG("(process %p)", process);
    return process->buffer.position - process->buffer.start;
}

int cbe_encode_get_container_level(cbe_encode_process* const process)
{
    KSLOG_DEBUG("(process %p)", process);
    return process->container.level;    
}

cbe_encode_status cbe_encode_add_padding(cbe_encode_process* const process, const int byte_count)
{
    KSLOG_DEBUG("(process %p, byte_count %d)", process, byte_count);
    unlikely_if(process == NULL || byte_count < 0)
    {
        return CBE_ENCODE_ERROR_INVALID_ARGUMENT;
    }

    STOP_AND_EXIT_IF_IS_INSIDE_ARRAY(process);
    STOP_AND_EXIT_IF_NOT_ENOUGH_ROOM_WITH_TYPE(process, byte_count);

    for(int i = 0; i < byte_count; i++)
    {
        add_primitive_uint8(process, TYPE_PADDING);
    }

    return CBE_ENCODE_STATUS_OK;
}

cbe_encode_status cbe_encode_add_nil(cbe_encode_process* const process)
{
    KSLOG_DEBUG("(process %p)", process);
    unlikely_if(process == NULL)
    {
        return CBE_ENCODE_ERROR_INVALID_ARGUMENT;
    }

    STOP_AND_EXIT_IF_IS_WRONG_MAP_KEY_TYPE(process);
    STOP_AND_EXIT_IF_IS_INSIDE_ARRAY(process);
    STOP_AND_EXIT_IF_NOT_ENOUGH_ROOM_WITH_TYPE(process, 0);

    add_primitive_type(process, TYPE_NIL);
    swap_map_key_value_status(process);

    return CBE_ENCODE_STATUS_OK;
}

cbe_encode_status cbe_encode_add_boolean(cbe_encode_process* const process, const bool value)
{
    KSLOG_DEBUG("(process %p, value %d)", process, value);
    unlikely_if(process == NULL)
    {
        return CBE_ENCODE_ERROR_INVALID_ARGUMENT;
    }

    STOP_AND_EXIT_IF_IS_INSIDE_ARRAY(process);
    STOP_AND_EXIT_IF_NOT_ENOUGH_ROOM_WITH_TYPE(process, 0);

    add_primitive_type(process, value ? TYPE_TRUE : TYPE_FALSE);
    swap_map_key_value_status(process);

    return CBE_ENCODE_STATUS_OK;
}

cbe_encode_status cbe_encode_add_integer(cbe_encode_process* const process, const int sign, const uint64_t value)
{
    KSLOG_DEBUG("(process %p, value %d)", process, value);
    unlikely_if(process == NULL)
    {
        return CBE_ENCODE_ERROR_INVALID_ARGUMENT;
    }
   
    if(FITS_IN_INT_SMALL(value))
    {
        return add_int_small(process, (int64_t)value * sign);
    }

    int is_negative = RSHIFT_MAX((unsigned)sign);

    if(FITS_IN_UINT_21(value))
    {
        if(FITS_IN_UINT_16(value))
        {
            if(FITS_IN_UINT_8(value)) return add_int_8(process, is_negative, value);
            return add_int_16(process, is_negative, value);
        }
        return add_int(process, is_negative, value);
    }
    if(FITS_IN_UINT_49(value))
    {
        if(FITS_IN_UINT_32(value)) return add_int_32(process, is_negative, value);
        return add_int(process, is_negative, value);
    }
    return add_int_64(process, is_negative, value);
}

cbe_encode_status cbe_encode_add_float(cbe_encode_process* const process, const double value, int significant_digits)
{
    KSLOG_DEBUG("(process %p, value %.16g)", process, value);
    unlikely_if(process == NULL)
    {
        return CBE_ENCODE_ERROR_INVALID_ARGUMENT;
    }

    if(significant_digits < 1 || significant_digits > 15)
    {
        if(FITS_IN_FLOAT_32(value)) return add_float_32(process, value);
        return add_float_64(process, value);
    }

    return add_float_decimal(process, value, significant_digits);
}

cbe_encode_status cbe_encode_add_decimal_float(cbe_encode_process* const process, const dec64_ct value, int significant_digits)
{
    KSLOG_DEBUG("(process %p, value ~= %.16g)", process, (double)value);
    unlikely_if(process == NULL)
    {
        return CBE_ENCODE_ERROR_INVALID_ARGUMENT;
    }

    return add_float_decimal(process, value, significant_digits);
}

#define FILL_TZ_STRING(TZ_PTR, TZ_STRING) \
    if(TZ_STRING == NULL) \
    { \
        (TZ_PTR)->type = CT_TZ_ZERO; \
    } \
    else \
    { \
        (TZ_PTR)->type = CT_TZ_STRING; \
        int length = strlen(TZ_STRING); \
        if((length + 1) > (int)sizeof((TZ_PTR)->as_string)) \
        { \
            return CBE_ENCODE_ERROR_INVALID_ARGUMENT; \
        } \
        memcpy((TZ_PTR)->as_string, TZ_STRING, length); \
        (TZ_PTR)->as_string[length] = 0; \
    }

#define ADD_TIME_COMMON(NAME_LOWER, NAME_UPPER) \
    STOP_AND_EXIT_IF_IS_INSIDE_ARRAY(process); \
    STOP_AND_EXIT_IF_NOT_ENOUGH_ROOM_WITH_TYPE(process, 1); \
    uint8_t* old_position = process->buffer.position; \
    \
    add_primitive_type(process, TYPE_##NAME_UPPER); \
    int bytes_encoded = ct_##NAME_LOWER##_encode(&NAME_LOWER, process->buffer.position, buff_remaining_length(process)); \
    if(bytes_encoded < 1) \
    { \
        KSLOG_DEBUG("Not enough room to encode " #NAME_LOWER); \
        process->buffer.position = old_position; \
        return CBE_ENCODE_STATUS_NEED_MORE_ROOM; \
    } \
    process->buffer.position += bytes_encoded; \
    \
    swap_map_key_value_status(process); \
    \
    return CBE_ENCODE_STATUS_OK

cbe_encode_status cbe_encode_add_date(struct cbe_encode_process* const process, int year, int month, int day)
{
    KSLOG_DEBUG("(process %p, date = %d.%02d.%02d)", process, year, month, day);
    ct_date date = {
        .year = year,
        .month = month,
        .day = day,
    };

    ADD_TIME_COMMON(date, DATE);
}

cbe_encode_status cbe_encode_add_time_tz(struct cbe_encode_process* const process, int hour, int minute, int second, int nanosecond, const char* tz_string)
{
    KSLOG_DEBUG("(process %p, time = %d:%02d:%02d.%09d/%s)", process, hour, minute, second, nanosecond, tz_string);
    ct_time time = {
        .hour = hour,
        .minute = minute,
        .second = second,
        .nanosecond = nanosecond,
        .timezone =
        {
            .type = CT_TZ_STRING,
        },
    };

    FILL_TZ_STRING(&time.timezone, tz_string);
    ADD_TIME_COMMON(time, TIME);
}

cbe_encode_status cbe_encode_add_time_loc(struct cbe_encode_process* const process, int hour, int minute, int second, int nanosecond, int latitude, int longitude)
{
    KSLOG_DEBUG("(process %p, time = %d:%02d:%02d.%09d/%d/%d)", process, hour, minute, second, nanosecond, latitude, longitude);
    ct_time time = {
        .hour = hour,
        .minute = minute,
        .second = second,
        .nanosecond = nanosecond,
        .timezone =
        {
            .type = CT_TZ_LATLONG,
            .latitude = latitude,
            .longitude = longitude,
        },
    };

    ADD_TIME_COMMON(time, TIME);
}

cbe_encode_status cbe_encode_add_timestamp_tz(struct cbe_encode_process* const process, int year, int month, int day, int hour, int minute, int second, int nanosecond, const char* tz_string)
{
    KSLOG_DEBUG("(process %p, ts = %d.%02d.%02d-%d:%02d:%02d.%09d/%s)", process, year, month, day, hour, minute, second, nanosecond, tz_string);
    ct_timestamp timestamp =
    {
        .date = {
            .year = year,
            .month = month,
            .day = day,
        },
        .time = {
            .hour = hour,
            .minute = minute,
            .second = second,
            .nanosecond = nanosecond,
            .timezone =
            {
                .type = CT_TZ_STRING,
            },
        }
    };

    FILL_TZ_STRING(&timestamp.time.timezone, tz_string);
    ADD_TIME_COMMON(timestamp, TIMESTAMP);
}

cbe_encode_status cbe_encode_add_timestamp_loc(struct cbe_encode_process* const process, int year, int month, int day, int hour, int minute, int second, int nanosecond, int latitude, int longitude)
{
    KSLOG_DEBUG("(process %p, ts = %d.%02d.%02d-%d:%02d:%02d.%09d/%d/%d)", process, year, month, day, hour, minute, second, nanosecond, latitude, longitude);
    ct_timestamp timestamp =
    {
        .date = {
            .year = year,
            .month = month,
            .day = day,
        },
        .time = {
            .hour = hour,
            .minute = minute,
            .second = second,
            .nanosecond = nanosecond,
            .timezone =
            {
                .type = CT_TZ_LATLONG,
                .latitude = latitude,
                .longitude = longitude,
            },
        }
    };

    ADD_TIME_COMMON(timestamp, TIMESTAMP);
}

cbe_encode_status cbe_encode_list_begin(cbe_encode_process* const process)
{
    KSLOG_DEBUG("(process %p)", process);
    unlikely_if(process == NULL)
    {
        return CBE_ENCODE_ERROR_INVALID_ARGUMENT;
    }

    STOP_AND_EXIT_IF_IS_INSIDE_ARRAY(process);
    STOP_AND_EXIT_IF_IS_WRONG_MAP_KEY_TYPE(process);
    STOP_AND_EXIT_IF_MAX_CONTAINER_DEPTH_EXCEEDED(process);
    STOP_AND_EXIT_IF_NOT_ENOUGH_ROOM_WITH_TYPE(process, 0);

    add_primitive_type(process, TYPE_LIST);
    swap_map_key_value_status(process);

    process->container.level++;
    process->is_inside_map[process->container.level] = false;
    process->container.next_object_is_map_key = false;

    return CBE_ENCODE_STATUS_OK;
}

cbe_encode_status cbe_encode_unordered_map_begin(cbe_encode_process* const process)
{
    KSLOG_DEBUG("(process %p)", process);
    unlikely_if(process == NULL)
    {
        return CBE_ENCODE_ERROR_INVALID_ARGUMENT;
    }

    STOP_AND_EXIT_IF_IS_INSIDE_ARRAY(process);
    STOP_AND_EXIT_IF_IS_WRONG_MAP_KEY_TYPE(process);
    STOP_AND_EXIT_IF_MAX_CONTAINER_DEPTH_EXCEEDED(process);
    STOP_AND_EXIT_IF_NOT_ENOUGH_ROOM_WITH_TYPE(process, 0);

    add_primitive_type(process, TYPE_MAP_UNORDERED);
    swap_map_key_value_status(process);

    process->container.level++;
    process->is_inside_map[process->container.level] = true;
    process->container.next_object_is_map_key = true;

    return CBE_ENCODE_STATUS_OK;
}

cbe_encode_status cbe_encode_ordered_map_begin(cbe_encode_process* const process)
{
    KSLOG_DEBUG("(process %p)", process);
    unlikely_if(process == NULL)
    {
        return CBE_ENCODE_ERROR_INVALID_ARGUMENT;
    }

    STOP_AND_EXIT_IF_IS_INSIDE_ARRAY(process);
    STOP_AND_EXIT_IF_IS_WRONG_MAP_KEY_TYPE(process);
    STOP_AND_EXIT_IF_MAX_CONTAINER_DEPTH_EXCEEDED(process);
    STOP_AND_EXIT_IF_NOT_ENOUGH_ROOM_WITH_TYPE(process, 0);

    add_primitive_type(process, TYPE_MAP_ORDERED);
    swap_map_key_value_status(process);

    process->container.level++;
    process->is_inside_map[process->container.level] = true;
    process->container.next_object_is_map_key = true;

    return CBE_ENCODE_STATUS_OK;
}

cbe_encode_status cbe_encode_metadata_map_begin(cbe_encode_process* const process)
{
    KSLOG_DEBUG("(process %p)", process);
    unlikely_if(process == NULL)
    {
        return CBE_ENCODE_ERROR_INVALID_ARGUMENT;
    }

    STOP_AND_EXIT_IF_IS_INSIDE_ARRAY(process);
    STOP_AND_EXIT_IF_IS_WRONG_MAP_KEY_TYPE(process);
    STOP_AND_EXIT_IF_MAX_CONTAINER_DEPTH_EXCEEDED(process);
    STOP_AND_EXIT_IF_NOT_ENOUGH_ROOM_WITH_TYPE(process, 0);

    add_primitive_type(process, TYPE_MAP_METADATA);
    swap_map_key_value_status(process);

    process->container.level++;
    process->is_inside_map[process->container.level] = true;
    process->container.next_object_is_map_key = true;

    return CBE_ENCODE_STATUS_OK;
}

cbe_encode_status cbe_encode_container_end(cbe_encode_process* const process)
{
    KSLOG_DEBUG("(process %p)", process);
    unlikely_if(process == NULL)
    {
        return CBE_ENCODE_ERROR_INVALID_ARGUMENT;
    }

    STOP_AND_EXIT_IF_IS_INSIDE_ARRAY(process);
    STOP_AND_EXIT_IF_IS_NOT_INSIDE_CONTAINER(process);
    STOP_AND_EXIT_IF_MAP_VALUE_MISSING(process);
    STOP_AND_EXIT_IF_NOT_ENOUGH_ROOM_WITH_TYPE(process, 0);

    add_primitive_type(process, TYPE_END_CONTAINER);
    process->container.level--;
    process->container.next_object_is_map_key = process->is_inside_map[process->container.level];

    return CBE_ENCODE_STATUS_OK;
}

cbe_encode_status cbe_encode_bytes_begin(cbe_encode_process* const process, const int64_t byte_count)
{
    KSLOG_DEBUG("(process %p, byte_count %d)", process, byte_count);
    unlikely_if(process == NULL || byte_count < 0)
    {
        return CBE_ENCODE_ERROR_INVALID_ARGUMENT;
    }

    STOP_AND_EXIT_IF_IS_INSIDE_ARRAY(process);
    STOP_AND_EXIT_IF_NOT_ENOUGH_ROOM_WITH_TYPE(process, get_array_length_field_width(byte_count));

    add_primitive_type(process, TYPE_BYTES);
    add_array_length_field(process, byte_count);
    begin_array(process, ARRAY_TYPE_BYTES, byte_count);
    swap_map_key_value_status(process);
    unlikely_if(byte_count == 0)
    {
        end_array(process);
    }

    return CBE_ENCODE_STATUS_OK;
}

cbe_encode_status cbe_encode_string_begin(cbe_encode_process* const process, const int64_t byte_count)
{
    KSLOG_DEBUG("(process %p, byte_count %d)", process, byte_count);
    unlikely_if(process == NULL || byte_count < 0)
    {
        return CBE_ENCODE_ERROR_INVALID_ARGUMENT;
    }

    STOP_AND_EXIT_IF_IS_INSIDE_ARRAY(process);

    const bool should_reserve_payload = false;
    return encode_string_header(process, byte_count, should_reserve_payload);
}

cbe_encode_status cbe_encode_uri_begin(cbe_encode_process* const process, const int64_t byte_count)
{
    KSLOG_DEBUG("(process %p, byte_count %d)", process, byte_count);
    unlikely_if(process == NULL || byte_count < 0)
    {
        return CBE_ENCODE_ERROR_INVALID_ARGUMENT;
    }

    STOP_AND_EXIT_IF_IS_INSIDE_ARRAY(process);
    STOP_AND_EXIT_IF_NOT_ENOUGH_ROOM_WITH_TYPE(process, get_array_length_field_width(byte_count));

    add_primitive_type(process, TYPE_URI);
    add_array_length_field(process, byte_count);
    begin_array(process, ARRAY_TYPE_URI, byte_count);
    swap_map_key_value_status(process);
    unlikely_if(byte_count == 0)
    {
        end_array(process);
    }

    return CBE_ENCODE_STATUS_OK;
}

cbe_encode_status cbe_encode_comment_begin(cbe_encode_process* const process, const int64_t byte_count)
{
    KSLOG_DEBUG("(process %p, byte_count %d)", process, byte_count);
    unlikely_if(process == NULL || byte_count < 0)
    {
        return CBE_ENCODE_ERROR_INVALID_ARGUMENT;
    }

    STOP_AND_EXIT_IF_IS_INSIDE_ARRAY(process);
    STOP_AND_EXIT_IF_NOT_ENOUGH_ROOM_WITH_TYPE(process, get_array_length_field_width(byte_count));

    add_primitive_type(process, TYPE_COMMENT);
    add_array_length_field(process, byte_count);
    begin_array(process, ARRAY_TYPE_COMMENT, byte_count);
    swap_map_key_value_status(process);
    unlikely_if(byte_count == 0)
    {
        end_array(process);
    }

    return CBE_ENCODE_STATUS_OK;
}

cbe_encode_status cbe_encode_add_data(cbe_encode_process* const process,
                                      const uint8_t* const start,
                                      int64_t* const byte_count)
{
    KSLOG_DEBUG("(process %p, start %p, byte_count %d)",
        process, start, byte_count == NULL ? -123456789 : *byte_count);
    unlikely_if(process == NULL || byte_count == NULL || *byte_count < 0)
    {
        return CBE_ENCODE_ERROR_INVALID_ARGUMENT;
    }
    unlikely_if(*byte_count == 0)
    {
        return CBE_ENCODE_STATUS_OK;
    }
    unlikely_if(start == NULL)
    {
        return CBE_ENCODE_ERROR_INVALID_ARGUMENT;
    }
    KSLOG_DATA_TRACE(start, *byte_count, NULL);

    STOP_AND_EXIT_IF_IS_NOT_INSIDE_ARRAY(process);
    STOP_AND_EXIT_IF_ARRAY_LENGTH_EXCEEDED(process, *byte_count);

    return encode_array_contents(process, (const uint8_t*)start, byte_count);
}

cbe_encode_status cbe_encode_add_string(cbe_encode_process* const process,
                                        const char* const string_start,
                                        const int64_t byte_count)
{
    uint8_t* const last_position = process->buffer.position;
    cbe_encode_status status = cbe_encode_string_begin(process, byte_count);
    unlikely_if(status != CBE_ENCODE_STATUS_OK)
    {
        return status;
    }
    int64_t byte_count_copy = byte_count;
    status = cbe_encode_add_data(process, (const uint8_t*)string_start, &byte_count_copy);
    unlikely_if(status != CBE_ENCODE_STATUS_OK)
    {
        process->buffer.position = last_position;
    }
    return status;
}

cbe_encode_status cbe_encode_add_bytes(cbe_encode_process* const process,
                                        const uint8_t* const data,
                                        const int64_t byte_count)
{
    uint8_t* const last_position = process->buffer.position;
    cbe_encode_status status = cbe_encode_bytes_begin(process, byte_count);
    unlikely_if(status != CBE_ENCODE_STATUS_OK)
    {
        return status;
    }
    int64_t byte_count_copy = byte_count;
    status = cbe_encode_add_data(process, data, &byte_count_copy);
    unlikely_if(status != CBE_ENCODE_STATUS_OK)
    {
        process->buffer.position = last_position;
    }
    return status;
}

cbe_encode_status cbe_encode_add_uri(cbe_encode_process* const process,
                                        const char* const uri_start,
                                        const int64_t byte_count)
{
    uint8_t* const last_position = process->buffer.position;
    cbe_encode_status status = cbe_encode_uri_begin(process, byte_count);
    unlikely_if(status != CBE_ENCODE_STATUS_OK)
    {
        return status;
    }
    int64_t byte_count_copy = byte_count;
    status = cbe_encode_add_data(process, (const uint8_t*)uri_start, &byte_count_copy);
    unlikely_if(status != CBE_ENCODE_STATUS_OK)
    {
        process->buffer.position = last_position;
    }
    return status;
}

cbe_encode_status cbe_encode_add_comment(cbe_encode_process* const process,
                                        const char* const comment_start,
                                        const int64_t byte_count)
{
    uint8_t* const last_position = process->buffer.position;
    cbe_encode_status status = cbe_encode_comment_begin(process, byte_count);
    unlikely_if(status != CBE_ENCODE_STATUS_OK)
    {
        return status;
    }
    int64_t byte_count_copy = byte_count;
    status = cbe_encode_add_data(process, (const uint8_t*)comment_start, &byte_count_copy);
    unlikely_if(status != CBE_ENCODE_STATUS_OK)
    {
        process->buffer.position = last_position;
    }
    return status;
}

cbe_encode_status cbe_encode_end(cbe_encode_process* const process)
{
    KSLOG_DEBUG("(process %p)", process);
    unlikely_if(process == NULL)
    {
        return CBE_ENCODE_ERROR_INVALID_ARGUMENT;
    }

    STOP_AND_EXIT_IF_IS_INSIDE_CONTAINER(process);
    STOP_AND_EXIT_IF_IS_INSIDE_ARRAY(process);

    KSLOG_DEBUG("Process ended successfully");
    return CBE_ENCODE_STATUS_OK;
}
