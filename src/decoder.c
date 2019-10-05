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

struct cbe_decode_process
{
    const cbe_decode_callbacks* callbacks;
    void* user_context;
    int64_t stream_offset;
    struct
    {
        const uint8_t* start;
        const uint8_t* end;
        const uint8_t* position;
        int64_t* bytes_consumed;
    } buffer;
    struct
    {
        bool is_inside_array;
        bool is_reading_byte_count;
        bool has_reported_byte_count;
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
typedef struct cbe_decode_process cbe_decode_process;


// ==============
// Utility Macros
// ==============

#define likely_if(TEST_FOR_TRUTH) if(__builtin_expect(TEST_FOR_TRUTH, 1))
#define unlikely_if(TEST_FOR_TRUTH) if(__builtin_expect(TEST_FOR_TRUTH, 0))

#define UPDATE_STREAM_OFFSET(PROCESS) \
    *(PROCESS)->buffer.bytes_consumed = (PROCESS)->buffer.position - (PROCESS)->buffer.start; \
    (PROCESS)->stream_offset += *(PROCESS)->buffer.bytes_consumed


// ==============
// Error Handlers
// ==============

#define STOP_AND_EXIT_IF_NOT_ENOUGH_ROOM(PROCESS, BYTE_COUNT) \
    unlikely_if(get_remaining_space_in_buffer(PROCESS) < (int64_t)(BYTE_COUNT)) \
    { \
        KSLOG_DEBUG("STOP AND EXIT: Require %d bytes but only %d available.", \
            (BYTE_COUNT), get_remaining_space_in_buffer(PROCESS)); \
        UPDATE_STREAM_OFFSET(PROCESS); \
        return CBE_DECODE_STATUS_NEED_MORE_DATA; \
    }

#define STOP_AND_EXIT_IF_READ_FAILED(PROCESS, ...) \
    { \
        int bytes_read = __VA_ARGS__; \
        unlikely_if(bytes_read <= 0) \
        { \
            KSLOG_DEBUG("STOP AND EXIT: Not enough space remaining to read data (%d).", bytes_read); \
            UPDATE_STREAM_OFFSET(PROCESS); \
            return CBE_DECODE_STATUS_NEED_MORE_DATA; \
        } \
        (PROCESS)->buffer.position += bytes_read; \
    }

#define STOP_AND_EXIT_IF_IS_INSIDE_ARRAY(PROCESS) \
    unlikely_if((PROCESS)->array.is_inside_array) \
    { \
        KSLOG_DEBUG("STOP AND EXIT: We're inside an array when we shouldn't be"); \
        UPDATE_STREAM_OFFSET(PROCESS); \
        return CBE_DECODE_ERROR_INCOMPLETE_ARRAY_FIELD; \
    }

#define STOP_AND_EXIT_IF_IS_INSIDE_CONTAINER(PROCESS) \
    unlikely_if((PROCESS)->container.level != 0) \
    { \
        KSLOG_DEBUG("STOP AND EXIT: There are still open containers when there shouldn't be"); \
        UPDATE_STREAM_OFFSET(PROCESS); \
        return CBE_DECODE_ERROR_UNBALANCED_CONTAINERS; \
    }

#define STOP_AND_EXIT_IF_MAP_VALUE_MISSING(PROCESS) \
    unlikely_if((PROCESS)->is_inside_map[(PROCESS)->container.level] && \
                !(PROCESS)->container.next_object_is_map_key) \
    { \
        KSLOG_DEBUG("STOP AND EXIT: Missing value for previous key"); \
        UPDATE_STREAM_OFFSET(PROCESS); \
        return CBE_DECODE_ERROR_MAP_MISSING_VALUE_FOR_KEY; \
    }

#define STOP_AND_EXIT_IF_IS_WRONG_MAP_KEY_TYPE(PROCESS) \
    unlikely_if((PROCESS)->is_inside_map[(PROCESS)->container.level] && \
                (PROCESS)->container.next_object_is_map_key) \
    { \
        KSLOG_DEBUG("STOP AND EXIT: Map key has an invalid type"); \
        KSLOG_TRACE("container_level: %d, is_inside_map: %d, next_object_is_map_key %d", \
            (PROCESS)->container.level, \
            (PROCESS)->is_inside_map[(PROCESS)->container.level], \
             (PROCESS)->container.next_object_is_map_key); \
        UPDATE_STREAM_OFFSET(PROCESS); \
        return CBE_DECODE_ERROR_INCORRECT_MAP_KEY_TYPE; \
    }

#define STOP_AND_EXIT_IF_DECODE_STATUS_NOT_OK(PROCESS, ...) \
{ \
    cbe_decode_status inner_status = __VA_ARGS__; \
    unlikely_if(inner_status != CBE_DECODE_STATUS_OK) \
    { \
        KSLOG_DEBUG("STOP AND EXIT: API call returned non-ok value %d", inner_status); \
        UPDATE_STREAM_OFFSET(PROCESS); \
        return inner_status; \
    } \
}

#define STOP_AND_EXIT_IF_FAILED_CALLBACK(PROCESS, ...) \
    unlikely_if(!__VA_ARGS__) \
    { \
        KSLOG_DEBUG("STOP AND EXIT: Callback returned false"); \
        UPDATE_STREAM_OFFSET(PROCESS); \
        return CBE_DECODE_STATUS_STOPPED_IN_CALLBACK; \
    }

#define STOP_AND_EXIT_IF_MAX_CONTAINER_DEPTH_EXCEEDED(PROCESS) \
    unlikely_if((PROCESS)->container.level + 1 >= (PROCESS)->container.max_depth) \
    { \
        KSLOG_DEBUG("STOP AND EXIT: Max depth %d exceeded", (PROCESS)->container.max_depth); \
        return CBE_DECODE_ERROR_MAX_CONTAINER_DEPTH_EXCEEDED; \
    }


// =======
// Utility
// =======

static inline int64_t get_remaining_space_in_buffer(cbe_decode_process* process)
{
    return process->buffer.end - process->buffer.position;
}

static inline void swap_map_key_value_status(cbe_decode_process* process)
{
    process->container.next_object_is_map_key = !process->container.next_object_is_map_key;
}

static inline int64_t consume_bytes(cbe_decode_process* const process, int64_t byte_count)
{
    process->buffer.position += byte_count;
    return byte_count;
}

static inline uint8_t peek_uint8(const cbe_decode_process* const process)
{
    return *process->buffer.position;
}

static inline uint8_t read_uint8(cbe_decode_process* const process)
{
    KSLOG_DATA_DEBUG(process->buffer.position, 1, NULL);
    return *process->buffer.position++;
}

#define DEFINE_READ_FUNCTION(TYPE, TYPE_SUFFIX) \
static inline TYPE read_ ## TYPE_SUFFIX(cbe_decode_process* const process) \
{ \
    TYPE value = read_##TYPE_SUFFIX##_le(process->buffer.position); \
    KSLOG_DATA_DEBUG(process->buffer.position, sizeof(value), NULL); \
    consume_bytes(process, sizeof(value)); \
    return value; \
}
DEFINE_READ_FUNCTION(uint16_t,    uint16)
DEFINE_READ_FUNCTION(uint32_t,    uint32)
DEFINE_READ_FUNCTION(uint64_t,    uint64)
DEFINE_READ_FUNCTION(float,       float32)
DEFINE_READ_FUNCTION(double,      float64)

static inline cbe_decode_status begin_object(cbe_decode_process* process, const int64_t initial_byte_count)
{
    KSLOG_DEBUG("(process %p, initial_byte_count %d)", process, initial_byte_count);
    STOP_AND_EXIT_IF_NOT_ENOUGH_ROOM(process, initial_byte_count);
    return CBE_DECODE_STATUS_OK;
}

static inline cbe_decode_status begin_nonkeyable_object(cbe_decode_process* process, const int64_t initial_byte_count)
{
    KSLOG_DEBUG("(process %p, initial_byte_count)", process, initial_byte_count);
    STOP_AND_EXIT_IF_IS_WRONG_MAP_KEY_TYPE(process);
    return begin_object(process, initial_byte_count);
}

static inline void end_object(cbe_decode_process* process)
{
    KSLOG_DEBUG("(process %p)", process);
    swap_map_key_value_status(process);
}

static cbe_decode_status begin_array(cbe_decode_process* const process, array_type type, int64_t byte_count)
{
    KSLOG_DEBUG("(process %p, array_type %d)", process, type);

    process->array.is_inside_array = true;
    process->array.has_reported_byte_count = false;
    process->array.type = type;
    process->array.current_offset = 0;
    process->array.is_reading_byte_count = byte_count < 0;
    process->array.byte_count = byte_count >= 0 ? byte_count : 0;

    return CBE_DECODE_STATUS_OK;
}

static cbe_decode_status stream_array(cbe_decode_process* const process)
{
    KSLOG_DEBUG("(process %p)", process);

    while(process->array.is_reading_byte_count)
    {
        STOP_AND_EXIT_IF_NOT_ENOUGH_ROOM(process, 1);
        uint8_t byte = read_uint8(process);
        KSLOG_DEBUG("Read byte %02x", byte);
        process->array.byte_count = process->array.byte_count << 7 | (byte & 0x7f);
        if((byte & 0x80) == 0)
        {
            process->array.is_reading_byte_count = false;
        }
        KSLOG_DEBUG("Byte count = %d, is_reading = %d", process->array.byte_count, process->array.is_reading_byte_count);
    }

    if(!process->array.has_reported_byte_count)
    {
        process->array.has_reported_byte_count = true;
        KSLOG_DEBUG("Length: %d", process->array.byte_count);
        switch(process->array.type)
        {
            case ARRAY_TYPE_BYTES:
                STOP_AND_EXIT_IF_FAILED_CALLBACK(process, process->callbacks->on_bytes_begin(process, process->array.byte_count));
                break;
            case ARRAY_TYPE_STRING:
                STOP_AND_EXIT_IF_FAILED_CALLBACK(process, process->callbacks->on_string_begin(process, process->array.byte_count));
                break;
            case ARRAY_TYPE_URI:
                STOP_AND_EXIT_IF_FAILED_CALLBACK(process, process->callbacks->on_uri_begin(process, process->array.byte_count));
                break;
            case ARRAY_TYPE_COMMENT:
                STOP_AND_EXIT_IF_FAILED_CALLBACK(process, process->callbacks->on_comment_begin(process, process->array.byte_count));
                break;
            default:
                KSLOG_ERROR("%d: Unknown array type", process->array.type);
                return CBE_DECODE_ERROR_INTERNAL_BUG;
        }
    }

    const int64_t bytes_in_array = process->array.byte_count - process->array.current_offset;
    const int64_t space_in_buffer = get_remaining_space_in_buffer(process);
    const int64_t bytes_to_stream = bytes_in_array <= space_in_buffer ? bytes_in_array : space_in_buffer;

    KSLOG_DEBUG("Length: arr %d vs buf %d: %d bytes", bytes_in_array, space_in_buffer, bytes_to_stream);
    KSLOG_DATA_TRACE(process->buffer.position, bytes_to_stream, NULL);
    switch(process->array.type)
    {
        case ARRAY_TYPE_BYTES:
            STOP_AND_EXIT_IF_FAILED_CALLBACK(process, process->callbacks->on_array_data(process, process->buffer.position, bytes_to_stream));
            break;
        case ARRAY_TYPE_STRING:
            if(!cbe_validate_string(process->buffer.position, bytes_to_stream))
            {
                return CBE_DECODE_ERROR_INVALID_ARRAY_DATA;
            }
            STOP_AND_EXIT_IF_FAILED_CALLBACK(process, process->callbacks->on_array_data(process, process->buffer.position, bytes_to_stream));
            break;
        case ARRAY_TYPE_URI:
            if(!cbe_validate_uri(process->buffer.position, bytes_to_stream))
            {
                return CBE_DECODE_ERROR_INVALID_ARRAY_DATA;
            }
            STOP_AND_EXIT_IF_FAILED_CALLBACK(process, process->callbacks->on_array_data(process, process->buffer.position, bytes_to_stream));
            break;
        case ARRAY_TYPE_COMMENT:
            if(!cbe_validate_comment(process->buffer.position, bytes_to_stream))
            {
                return CBE_DECODE_ERROR_INVALID_ARRAY_DATA;
            }
            STOP_AND_EXIT_IF_FAILED_CALLBACK(process, process->callbacks->on_array_data(process, process->buffer.position, bytes_to_stream));
            break;
        default:
            KSLOG_ERROR("%d: Unknown array type", process->array.type);
            return CBE_DECODE_ERROR_INTERNAL_BUG;
    }
    consume_bytes(process, bytes_to_stream);
    process->array.current_offset += bytes_to_stream;

    KSLOG_DEBUG("Streamed %d bytes into array", bytes_to_stream);
    STOP_AND_EXIT_IF_NOT_ENOUGH_ROOM(process, bytes_in_array - space_in_buffer);
    end_object(process);
    process->array.is_inside_array = false;

    return CBE_DECODE_STATUS_OK;
}


// ===
// API
// ===

int cbe_decode_process_size(const int max_container_depth)
{
    KSLOG_DEBUG("(max_container_depth %d)", max_container_depth);
    return sizeof(cbe_decode_process) + get_max_container_depth_or_default(max_container_depth);
}

cbe_decode_status cbe_decode_begin(cbe_decode_process* const process,
                                   const cbe_decode_callbacks* const callbacks,
                                   void* const user_context,
                                   const int max_container_depth)
{
    KSLOG_DEBUG("(process %p, callbacks %p, user_context %p)", process, callbacks, user_context);
    unlikely_if(process == NULL || callbacks == NULL)
    {
        return CBE_DECODE_ERROR_INVALID_ARGUMENT;
    }

    zero_memory(process, sizeof(*process) + 1);
    process->callbacks = callbacks;
    process->user_context = user_context;
    process->container.max_depth = get_max_container_depth_or_default(max_container_depth);

    return CBE_DECODE_STATUS_OK;
}

void* cbe_decode_get_user_context(cbe_decode_process* const process)
{
    KSLOG_DEBUG("(process %p)", process);
    return process->user_context;
}

cbe_decode_status cbe_decode_feed(cbe_decode_process* const process,
                                  const uint8_t* const data_start,
                                  int64_t* const byte_count)
{
    KSLOG_DEBUG("(process %p, data_start %p, byte_count %ld)", process, data_start, byte_count == NULL ? -123456789 : *byte_count);
    unlikely_if(process == NULL || data_start == NULL || byte_count == NULL || *byte_count < 0)
    {
        return CBE_DECODE_ERROR_INVALID_ARGUMENT;
    }

    KSLOG_DATA_TRACE(data_start, *byte_count, NULL);

    process->buffer.start = data_start;
    process->buffer.position = data_start;
    process->buffer.end = data_start + *byte_count;
    process->buffer.bytes_consumed = byte_count;

    #define BEGIN_OBJECT(SIZE) \
        STOP_AND_EXIT_IF_DECODE_STATUS_NOT_OK(process, begin_object(process, SIZE));
    #define BEGIN_NONKEYABLE_OBJECT(SIZE) \
        STOP_AND_EXIT_IF_DECODE_STATUS_NOT_OK(process, begin_nonkeyable_object(process, SIZE));
    #define END_OBJECT() \
        end_object(process)

    unlikely_if(process->array.is_inside_array)
    {
        STOP_AND_EXIT_IF_DECODE_STATUS_NOT_OK(process, stream_array(process));
    }

    while(process->buffer.position < process->buffer.end)
    {
        const cbe_type_field type = read_uint8(process);

        switch(type)
        {
            case TYPE_PADDING:
                KSLOG_DEBUG("<Padding>");
                // Ignore and restart loop because padding doesn't count as document content.
                // Otherwise the document depth test would exit the decode loop.
                continue;
            case TYPE_NIL:
                KSLOG_DEBUG("<Nil>");
                BEGIN_NONKEYABLE_OBJECT(0);
                STOP_AND_EXIT_IF_FAILED_CALLBACK(process, process->callbacks->on_nil(process));
                END_OBJECT();
                break;
            case TYPE_FALSE:
                KSLOG_DEBUG("<False>");
                BEGIN_OBJECT(0);
                STOP_AND_EXIT_IF_FAILED_CALLBACK(process, process->callbacks->on_boolean(process, false));
                END_OBJECT();
                break;
            case TYPE_TRUE:
                KSLOG_DEBUG("<True>");
                BEGIN_OBJECT(0);
                STOP_AND_EXIT_IF_FAILED_CALLBACK(process, process->callbacks->on_boolean(process, true));
                END_OBJECT();
                break;
            case TYPE_LIST:
                KSLOG_DEBUG("<List>");
                STOP_AND_EXIT_IF_MAX_CONTAINER_DEPTH_EXCEEDED(process)
                BEGIN_NONKEYABLE_OBJECT(0);
                STOP_AND_EXIT_IF_FAILED_CALLBACK(process, process->callbacks->on_list_begin(process));
                process->container.level++;
                process->is_inside_map[process->container.level] = false;
                process->container.next_object_is_map_key = false;
                break;
            case TYPE_MAP_UNORDERED:
                KSLOG_DEBUG("<Map Unordered>");
                STOP_AND_EXIT_IF_MAX_CONTAINER_DEPTH_EXCEEDED(process)
                BEGIN_NONKEYABLE_OBJECT(0);
                STOP_AND_EXIT_IF_FAILED_CALLBACK(process, process->callbacks->on_unordered_map_begin(process));
                process->container.level++;
                process->is_inside_map[process->container.level] = true;
                process->container.next_object_is_map_key = true;
                break;
            case TYPE_MAP_ORDERED:
                KSLOG_DEBUG("<Map Ordered>");
                STOP_AND_EXIT_IF_MAX_CONTAINER_DEPTH_EXCEEDED(process)
                BEGIN_NONKEYABLE_OBJECT(0);
                STOP_AND_EXIT_IF_FAILED_CALLBACK(process, process->callbacks->on_ordered_map_begin(process));
                process->container.level++;
                process->is_inside_map[process->container.level] = true;
                process->container.next_object_is_map_key = true;
                break;
            case TYPE_MAP_METADATA:
                KSLOG_DEBUG("<Map Metadata>");
                STOP_AND_EXIT_IF_MAX_CONTAINER_DEPTH_EXCEEDED(process)
                BEGIN_NONKEYABLE_OBJECT(0);
                STOP_AND_EXIT_IF_FAILED_CALLBACK(process, process->callbacks->on_metadata_map_begin(process));
                process->container.level++;
                process->is_inside_map[process->container.level] = true;
                process->container.next_object_is_map_key = true;
                break;
            case TYPE_END_CONTAINER:
                KSLOG_DEBUG("<End Container>");
                STOP_AND_EXIT_IF_MAP_VALUE_MISSING(process);
                STOP_AND_EXIT_IF_FAILED_CALLBACK(process, process->callbacks->on_container_end(process));
                END_OBJECT();
                process->container.level--;
                process->container.next_object_is_map_key = process->is_inside_map[process->container.level];
                break;

            case TYPE_STRING_0: case TYPE_STRING_1: case TYPE_STRING_2: case TYPE_STRING_3:
            case TYPE_STRING_4: case TYPE_STRING_5: case TYPE_STRING_6: case TYPE_STRING_7:
            case TYPE_STRING_8: case TYPE_STRING_9: case TYPE_STRING_10: case TYPE_STRING_11:
            case TYPE_STRING_12: case TYPE_STRING_13: case TYPE_STRING_14: case TYPE_STRING_15:
            {
                const int64_t array_byte_count = (int64_t)(type - TYPE_STRING_0);
                KSLOG_DEBUG("<String %d>", array_byte_count);
                begin_array(process, ARRAY_TYPE_STRING, array_byte_count);
                STOP_AND_EXIT_IF_DECODE_STATUS_NOT_OK(process, stream_array(process));
                break;
            }

            #define HANDLE_CASE_INTEGER(TYPE, SIGN, READ_FRAGMENT, NOTIFY_FRAGMENT) \
                KSLOG_DEBUG("<" #TYPE ">"); \
                BEGIN_OBJECT(sizeof(TYPE)); \
                STOP_AND_EXIT_IF_FAILED_CALLBACK(process, process->callbacks->on_ ## NOTIFY_FRAGMENT(process, SIGN, read_ ## READ_FRAGMENT(process))); \
                END_OBJECT();

            case TYPE_INT_NEG_8:
                HANDLE_CASE_INTEGER(uint8_t, -1, uint8, integer);
                break;
            case TYPE_INT_NEG_16:
                HANDLE_CASE_INTEGER(uint16_t, -1, uint16, integer);
                break;
            case TYPE_INT_NEG_32:
                HANDLE_CASE_INTEGER(uint32_t, -1, uint32, integer);
                break;
            case TYPE_INT_NEG_64:
                HANDLE_CASE_INTEGER(uint64_t, -1, uint64, integer);
                break;
            case TYPE_INT_NEG:
            {
                uint64_t value = 0;
                STOP_AND_EXIT_IF_READ_FAILED(process, rvlq_decode_64(&value, process->buffer.position, process->buffer.end - process->buffer.position));
                STOP_AND_EXIT_IF_FAILED_CALLBACK(process, process->callbacks->on_integer(process, -1, value));
                break;
            }
            case TYPE_INT_POS_8:
                HANDLE_CASE_INTEGER(uint8_t, 1, uint8, integer);
                break;
            case TYPE_INT_POS_16:
                HANDLE_CASE_INTEGER(uint16_t, 1, uint16, integer);
                break;
            case TYPE_INT_POS_32:
                HANDLE_CASE_INTEGER(uint32_t, 1, uint32, integer);
                break;
            case TYPE_INT_POS_64:
                HANDLE_CASE_INTEGER(uint64_t, 1, uint64, integer);
                break;
            case TYPE_INT_POS:
            {
                uint64_t value = 0;
                STOP_AND_EXIT_IF_READ_FAILED(process, rvlq_decode_64(&value, process->buffer.position, process->buffer.end - process->buffer.position));
                STOP_AND_EXIT_IF_FAILED_CALLBACK(process, process->callbacks->on_integer(process, 1, value));
                break;
            }

            #define HANDLE_CASE_SCALAR(TYPE, READ_FRAGMENT, NOTIFY_FRAGMENT) \
                KSLOG_DEBUG("<" #TYPE ">"); \
                BEGIN_OBJECT(sizeof(TYPE)); \
                STOP_AND_EXIT_IF_FAILED_CALLBACK(process, process->callbacks->on_ ## NOTIFY_FRAGMENT(process, read_ ## READ_FRAGMENT(process))); \
                END_OBJECT();

            case TYPE_FLOAT_BINARY_32:
                HANDLE_CASE_SCALAR(float, float32, float);
                break;
            case TYPE_FLOAT_BINARY_64:
                HANDLE_CASE_SCALAR(double, float64, float);
                break;
            case TYPE_FLOAT_DECIMAL:
            {
                dec64_ct value = 0;
                STOP_AND_EXIT_IF_READ_FAILED(process, cfloat_decode(process->buffer.position, process->buffer.end - process->buffer.position, &value));
                STOP_AND_EXIT_IF_FAILED_CALLBACK(process, process->callbacks->on_decimal_float(process, value));
                break;
            }
            case TYPE_DATE:
            {
                ct_date v;
                STOP_AND_EXIT_IF_READ_FAILED(process, ct_date_decode(process->buffer.position,
                    process->buffer.end - process->buffer.position, &v));
                STOP_AND_EXIT_IF_FAILED_CALLBACK(process,
                    process->callbacks->on_date(process, v.year, v.month, v.day));
                break;
            }
            case TYPE_TIME:
            {
                ct_time v;
                STOP_AND_EXIT_IF_READ_FAILED(process, ct_time_decode(process->buffer.position,
                    process->buffer.end - process->buffer.position, &v));
                switch(v.timezone.type)
                {
                    case CT_TZ_ZERO:
                        STOP_AND_EXIT_IF_FAILED_CALLBACK(process,
                            process->callbacks->on_time_tz(process, v.hour, v.minute, v.second, v.nanosecond, NULL));
                        break;
                    case CT_TZ_STRING:
                        STOP_AND_EXIT_IF_FAILED_CALLBACK(process,
                            process->callbacks->on_time_tz(process, v.hour, v.minute, v.second,
                                v.nanosecond, v.timezone.as_string));
                        break;
                    case CT_TZ_LATLONG:
                        STOP_AND_EXIT_IF_FAILED_CALLBACK(process,
                            process->callbacks->on_time_loc(process, v.hour, v.minute, v.second,
                                v.nanosecond, v.timezone.latitude, v.timezone.longitude));
                        break;
                }
                break;
            }
            case TYPE_TIMESTAMP:
            {
                ct_timestamp v;
                STOP_AND_EXIT_IF_READ_FAILED(process, ct_timestamp_decode(process->buffer.position,
                    process->buffer.end - process->buffer.position, &v));
                switch(v.time.timezone.type)
                {
                    case CT_TZ_ZERO:
                        STOP_AND_EXIT_IF_FAILED_CALLBACK(process,
                            process->callbacks->on_timestamp_tz(process, v.date.year, v.date.month, v.date.day,
                                v.time.hour, v.time.minute, v.time.second, v.time.nanosecond, NULL));
                        break;
                    case CT_TZ_STRING:
                        STOP_AND_EXIT_IF_FAILED_CALLBACK(process,
                            process->callbacks->on_timestamp_tz(process, v.date.year, v.date.month, v.date.day,
                                v.time.hour, v.time.minute, v.time.second, v.time.nanosecond, v.time.timezone.as_string));
                        break;
                    case CT_TZ_LATLONG:
                        STOP_AND_EXIT_IF_FAILED_CALLBACK(process,
                            process->callbacks->on_timestamp_loc(process, v.date.year, v.date.month, v.date.day,
                                v.time.hour, v.time.minute, v.time.second, v.time.nanosecond,
                                v.time.timezone.latitude, v.time.timezone.longitude));
                        break;
                }
                break;
            }
                break;
            case TYPE_STRING:
            {
                KSLOG_DEBUG("<String>");
                STOP_AND_EXIT_IF_DECODE_STATUS_NOT_OK(process, begin_array(process, ARRAY_TYPE_STRING, -1));
                STOP_AND_EXIT_IF_DECODE_STATUS_NOT_OK(process, stream_array(process));
                break;
            }
            case TYPE_BYTES:
            {
                KSLOG_DEBUG("<Bytes>");
                STOP_AND_EXIT_IF_DECODE_STATUS_NOT_OK(process, begin_array(process, ARRAY_TYPE_BYTES, -1));
                STOP_AND_EXIT_IF_DECODE_STATUS_NOT_OK(process, stream_array(process));
                break;
            }
            case TYPE_URI:
            {
                KSLOG_DEBUG("<URI>");
                STOP_AND_EXIT_IF_DECODE_STATUS_NOT_OK(process, begin_array(process, ARRAY_TYPE_URI, -1));
                STOP_AND_EXIT_IF_DECODE_STATUS_NOT_OK(process, stream_array(process));
                break;
            }
            case TYPE_COMMENT:
            {
                KSLOG_DEBUG("<Comment>");
                STOP_AND_EXIT_IF_DECODE_STATUS_NOT_OK(process, begin_array(process, ARRAY_TYPE_COMMENT, -1));
                STOP_AND_EXIT_IF_DECODE_STATUS_NOT_OK(process, stream_array(process));
                break;
            }
            default:
                if(type < TYPE_SMALLINT_MIN || type > TYPE_SMALLINT_MAX)
                {
                    // TODO: Error
                }
                KSLOG_DEBUG("<Small %d>", (int8_t)type);
                BEGIN_OBJECT(0);
                if((int8_t)type < 0)
                {
                    STOP_AND_EXIT_IF_FAILED_CALLBACK(process, process->callbacks->on_integer(process, -1, (int8_t)-type));
                }
                else
                {
                    STOP_AND_EXIT_IF_FAILED_CALLBACK(process, process->callbacks->on_integer(process, 1, (uint8_t)type));
                }
                END_OBJECT();
                break;
        }
        unlikely_if(process->container.level <= 0)
        {
            break;
        }
    }
    UPDATE_STREAM_OFFSET(process);
    return CBE_DECODE_STATUS_OK;
}

int64_t cbe_decode_get_stream_offset(cbe_decode_process* const process)
{
    KSLOG_DEBUG("(process %p)", process);
    unlikely_if(process == NULL)
    {
        return CBE_DECODE_ERROR_INVALID_ARGUMENT;
    }

    return process->stream_offset;
}

cbe_decode_status cbe_decode_end(cbe_decode_process* const process)
{
    KSLOG_DEBUG("(process %p)", process);
    unlikely_if(process == NULL)
    {
        return CBE_DECODE_ERROR_INVALID_ARGUMENT;
    }

    STOP_AND_EXIT_IF_IS_INSIDE_CONTAINER(process);
    STOP_AND_EXIT_IF_IS_INSIDE_ARRAY(process);

    KSLOG_DEBUG("Process ended successfully");
    return CBE_DECODE_STATUS_OK;
}

cbe_decode_status cbe_decode(const cbe_decode_callbacks* const callbacks,
                             void* const user_context,
                             const uint8_t* const document,
                             const int64_t document_length,
                             const int max_container_depth)
{
    KSLOG_DEBUG("(callbacks %p, user_context %p, document %p, document_length %d, max_container_depth %d)",
        callbacks, user_context, document, document_length, max_container_depth);
    unlikely_if(callbacks == NULL || document == NULL || document_length < 0)
    {
        return CBE_DECODE_ERROR_INVALID_ARGUMENT;
    }

    char decode_process_backing_store[cbe_decode_process_size(max_container_depth)];
    cbe_decode_process* process = (cbe_decode_process*)decode_process_backing_store;
    cbe_decode_status status = cbe_decode_begin(process, callbacks, user_context, max_container_depth);
    unlikely_if(status != CBE_DECODE_STATUS_OK)
    {
        return status;
    }

    int64_t byte_count = document_length;
    status = cbe_decode_feed(process, document, &byte_count);
    unlikely_if(status != CBE_DECODE_STATUS_OK && status != CBE_DECODE_STATUS_NEED_MORE_DATA)
    {
        return status;
    }

    return cbe_decode_end(process);
}
