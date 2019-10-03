#include "encoder.h"

// #define KSLog_LocalMinLevel KSLOG_LEVEL_TRACE
#include <kslog/kslog.h>

bool encoder::flush_buffer()
{
    bool result = false;
    int64_t offset = cbe_encode_get_buffer_offset(_process);
    KSLOG_DEBUG("Flushing %d bytes and resetting buffer to %d bytes", offset, _buffer.size());
    if(offset > 0)
    {
        _encoded_data.insert(_encoded_data.end(), _buffer.begin(), _buffer.begin() + offset);
        result = _on_data_ready(_buffer.data(), offset);
    }
    if(cbe_encode_set_buffer(_process, _buffer.data(), _buffer.size()) != CBE_ENCODE_STATUS_OK)
    {
        KSLOG_ERROR("Error setting encode buffer");
        return false;
    }

    return result;
}

cbe_encode_status encoder::flush_and_retry(std::function<cbe_encode_status()> my_function)
{
    cbe_encode_status status = my_function();
    if(status == CBE_ENCODE_STATUS_NEED_MORE_ROOM)
    {
        flush_buffer();
        status = my_function();
    }
    return status;
}

cbe_encode_status encoder::stream_array(const std::vector<uint8_t>& data)
{
    const uint8_t* data_pointer = data.data();
    const int64_t total_byte_count = data.size();
    const uint8_t* const data_end = data_pointer + total_byte_count;
    KSLOG_DEBUG("Streaming %d bytes", total_byte_count);

    cbe_encode_status status = CBE_ENCODE_STATUS_OK;

    while(data_pointer <= data_end)
    {
        int64_t byte_count = data_end - data_pointer;
        status = cbe_encode_add_data(_process, data_pointer, &byte_count);
        if(status != CBE_ENCODE_STATUS_NEED_MORE_ROOM)
        {
            break;
        }
        KSLOG_DEBUG("Streamed %d bytes", byte_count);
        flush_buffer();
        data_pointer += byte_count;
    }

    KSLOG_DEBUG("Done");
    return status;
}


cbe_encode_status encoder::encode(const encoding::value& v)
{
    cbe_encode_status status = flush_and_retry([&]
    {
        switch(v.type)
        {
            case encoding::value::type_int_pos:
                return cbe_encode_add_integer(_process, 1, v.i);
            case encoding::value::type_int_neg:
                return cbe_encode_add_integer(_process, -1, v.i);
            case encoding::value::type_float:
                return cbe_encode_add_float(_process, v.f, v.i);
            case encoding::value::type_decfloat:
                return cbe_encode_add_decimal_float(_process, v.df, v.i);
            case encoding::value::type_bool:
                return cbe_encode_add_boolean(_process, v.b);
            case encoding::value::type_date:
                return CBE_ENCODE_STATUS_OK; // TODO
            case encoding::value::type_time:
                return CBE_ENCODE_STATUS_OK; // TODO
            case encoding::value::type_ts:
                return CBE_ENCODE_STATUS_OK; // TODO
            case encoding::value::type_str:
                return cbe_encode_string_begin(_process, v.str.size());
            case encoding::value::type_bin:
                return cbe_encode_bytes_begin(_process, v.bin.size());
            case encoding::value::type_uri:
                return cbe_encode_uri_begin(_process, v.str.size());
            case encoding::value::type_com:
                return cbe_encode_comment_begin(_process, v.str.size());
            case encoding::value::type_strh:
                return cbe_encode_string_begin(_process, v.i);
            case encoding::value::type_urih:
                return cbe_encode_uri_begin(_process, v.i);
            case encoding::value::type_comh:
                return cbe_encode_comment_begin(_process, v.i);
            case encoding::value::type_binh:
                return cbe_encode_bytes_begin(_process, v.i);
            case encoding::value::type_data:
                // Contents handled outside
                return CBE_ENCODE_STATUS_OK;
            case encoding::value::type_list:
                return cbe_encode_list_begin(_process);
            case encoding::value::type_map_u:
                return cbe_encode_unordered_map_begin(_process);
            case encoding::value::type_map_o:
                return cbe_encode_ordered_map_begin(_process);
            case encoding::value::type_map_m:
                return cbe_encode_metadata_map_begin(_process);
            case encoding::value::type_end:
                return cbe_encode_container_end(_process);
            case encoding::value::type_nil:
                return cbe_encode_add_nil(_process);
            case encoding::value::type_pad:
                return cbe_encode_add_padding(_process, v.i);
            default:
                break;
        }
        KSLOG_ERROR("Unknown value type %d", v.type);
        return (cbe_encode_status)1999999;
    });

    if(status != CBE_ENCODE_STATUS_OK)
    {
        return status;
    }

    switch(v.type)
    {
        case encoding::value::type_str:
        case encoding::value::type_uri:
        case encoding::value::type_com:
            return stream_array(std::vector<uint8_t>(v.str.begin(), v.str.end()));
        case encoding::value::type_bin:
        case encoding::value::type_data:
            return stream_array(std::vector<uint8_t>(v.bin.begin(), v.bin.end()));
        default:
            break;
    }

    return status;
}

cbe_encode_status encoder::encode_string(std::vector<uint8_t> value)
{
    cbe_encode_status result = cbe_encode_begin(_process, _buffer.data(), _buffer.size(), _max_container_depth);
    if(result != CBE_ENCODE_STATUS_OK)
    {
        return result;
    }

    return cbe_encode_add_string(_process, (const char*)value.data(), value.size());
}

cbe_encode_status encoder::encode_uri(std::vector<uint8_t> value)
{
    cbe_encode_status result = cbe_encode_begin(_process, _buffer.data(), _buffer.size(), _max_container_depth);
    if(result != CBE_ENCODE_STATUS_OK)
    {
        return result;
    }

    return cbe_encode_add_uri(_process, (const char*)value.data(), value.size());
}

cbe_encode_status encoder::encode_comment(std::vector<uint8_t> value)
{
    cbe_encode_status result = cbe_encode_begin(_process, _buffer.data(), _buffer.size(), _max_container_depth);
    if(result != CBE_ENCODE_STATUS_OK)
    {
        return result;
    }

    return cbe_encode_add_comment(_process, (const char*)value.data(), value.size());
}

cbe_encode_status encoder::encode_bytes(std::vector<uint8_t> value)
{
    cbe_encode_status result = cbe_encode_begin(_process, _buffer.data(), _buffer.size(), _max_container_depth);
    if(result != CBE_ENCODE_STATUS_OK)
    {
        return result;
    }

    return cbe_encode_add_bytes(_process, value.data(), value.size());
}

cbe_encode_status encoder::encode(const encoding::enc& enc)
{
    cbe_encode_status result = cbe_encode_begin(_process, _buffer.data(), _buffer.size(), _max_container_depth);
    if(result != CBE_ENCODE_STATUS_OK)
    {
        return result;
    }

    for(auto i: enc.values)
    {
        result = encode(i);
        if(result != CBE_ENCODE_STATUS_OK)
        {
            flush_buffer();
            return result;
        }
    }

    flush_buffer();
    return cbe_encode_end(_process);
}

int64_t encoder::get_encode_buffer_offset() const
{
    return cbe_encode_get_buffer_offset(_process);
}

encoder::encoder(int64_t buffer_size,
                 int max_container_depth,
                 std::function<bool(uint8_t* data_start, int64_t length)> on_data_ready)
: _process_backing_store(cbe_encode_process_size(max_container_depth))
, _process((cbe_encode_process*)_process_backing_store.data())
, _buffer(buffer_size)
, _max_container_depth(max_container_depth)
, _on_data_ready(on_data_ready)
{
    KSLOG_DEBUG("New encoder with buffer size %d", _buffer.size());
}
