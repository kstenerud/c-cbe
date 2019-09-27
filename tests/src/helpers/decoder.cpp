#include "decoder.h"

// #define KSLog_LocalMinLevel KSLOG_LEVEL_TRACE
#include <kslog/kslog.h>

#include "test_utils.h"

static decoder* get_decoder(struct cbe_decode_process* process)
{
    return (decoder*)cbe_decode_get_user_context(process);
}

#define DEFINE_CALLBACK_0(NAME) \
static bool NAME(struct cbe_decode_process* process) \
{return get_decoder(process)->NAME();}

#define DEFINE_CALLBACK_1(NAME, TYPE) \
static bool NAME(struct cbe_decode_process* process, TYPE param) \
{return get_decoder(process)->NAME(param);}

#define DEFINE_CALLBACK_2(NAME, TYPE1, TYPE2) \
static bool NAME(struct cbe_decode_process* process, TYPE1 param1, TYPE2 param2) \
{return get_decoder(process)->NAME(param1, param2);}

DEFINE_CALLBACK_0(on_nil)
DEFINE_CALLBACK_1(on_boolean, bool)
DEFINE_CALLBACK_2(on_integer, int, uint64_t)
DEFINE_CALLBACK_1(on_float, double)
DEFINE_CALLBACK_0(on_list_begin)
DEFINE_CALLBACK_0(on_unordered_map_begin)
DEFINE_CALLBACK_0(on_ordered_map_begin)
DEFINE_CALLBACK_0(on_metadata_map_begin)
DEFINE_CALLBACK_0(on_container_end)
DEFINE_CALLBACK_1(on_string_begin, int64_t)
DEFINE_CALLBACK_1(on_bytes_begin, int64_t)
DEFINE_CALLBACK_1(on_uri_begin, int64_t)
DEFINE_CALLBACK_1(on_comment_begin, int64_t)
DEFINE_CALLBACK_2(on_array_data, const uint8_t*, int64_t)

// static bool on_array_data(struct cbe_decode_process* process, const uint8_t* start, int64_t byte_count)
// {
//     return get_decoder(process)->add_bytes_data(std::vector<uint8_t>(start, start + byte_count));
// }


ANSI_EXTENSION static const cbe_decode_callbacks g_callbacks =
{
    on_nil: on_nil,
    on_boolean: on_boolean,
    on_integer: on_integer,
    on_float: on_float,
    on_list_begin: on_list_begin,
    on_unordered_map_begin: on_unordered_map_begin,
    on_ordered_map_begin: on_ordered_map_begin,
    on_metadata_map_begin: on_metadata_map_begin,
    on_container_end: on_container_end,
    on_string_begin: on_string_begin,
    on_bytes_begin: on_bytes_begin,
    on_uri_begin: on_uri_begin,
    on_comment_begin: on_comment_begin,
    on_array_data: on_array_data,
};

decoder::decoder(int max_container_depth, bool forced_callback_return_value)
: _process_backing_store(cbe_decode_process_size(max_container_depth))
, _process((cbe_decode_process*)_process_backing_store.data())
, _forced_callback_return_value(forced_callback_return_value)
, _max_container_depth(max_container_depth)
{
}

std::vector<uint8_t>& decoder::received_data()
{
    return _received_data;
}

encoding::enc decoder::decoded()
{
    return _enc;
}

cbe_decode_status decoder::feed(const std::vector<uint8_t>& data)
{
    KSLOG_DEBUG("Feeding %d bytes", data.size());
    KSLOG_TRACE("Feeding %s", as_string(data).c_str());
    _received_data.insert(_received_data.begin(), data.begin(), data.end());
    int64_t byte_count = data.size();
    cbe_decode_status status = cbe_decode_feed(_process, data.data(), &byte_count);
    _read_offset += byte_count;
    return status;
}

cbe_decode_status decoder::begin()
{
    return cbe_decode_begin(_process, &g_callbacks, (void*)this, _max_container_depth);
}

cbe_decode_status decoder::end()
{
    if(!_process_is_valid)
    {
        KSLOG_ERROR("Called end() too many times");
        return (cbe_decode_status)9999999;
    }
    _process_is_valid = false;
    return cbe_decode_end(_process);
}

cbe_decode_status decoder::decode(const std::vector<uint8_t>& document)
{
    return cbe_decode(&g_callbacks, (void*)this, document.data(), document.size(), _max_container_depth);
}

bool decoder::mark_complete(encoding::value v)
{
    if(!_forced_callback_return_value) return false;

    if(_currently_decoding_type != CBE_DECODING_OTHER)
    {
        KSLOG_ERROR("Expected _currently_decoding_type OTHER, not %d", _currently_decoding_type);
        return false;
    }
    _enc.add(v);
    return true;
}

bool decoder::on_nil()
{
    return this->mark_complete(encoding::value::nilv());
}

bool decoder::on_boolean(bool value)
{
    return this->mark_complete(encoding::value::bv(value));
}

bool decoder::on_integer(int sign, uint64_t value)
{
    return this->mark_complete(encoding::value::iv(sign, value));
}

bool decoder::on_float(double value)
{
    return this->mark_complete(encoding::value::fv(value));
}

bool decoder::on_decimal(dec64_ct value)
{
    // TODO
    (void)value;
    return false;
}

bool decoder::on_list_begin()
{
    return this->mark_complete(encoding::value::listv());
}

bool decoder::on_unordered_map_begin()
{
    return this->mark_complete(encoding::value::umapv());
}

bool decoder::on_ordered_map_begin()
{
    return this->mark_complete(encoding::value::omapv());
}

bool decoder::on_metadata_map_begin()
{
    return this->mark_complete(encoding::value::mmapv());
}

bool decoder::on_container_end()
{
    return this->mark_complete(encoding::value::endv());
}

bool decoder::on_string_begin(int64_t byte_count)
{
    if(!_forced_callback_return_value) return false;

    if(_currently_decoding_type != CBE_DECODING_OTHER)
    {
        KSLOG_ERROR("Expected _currently_decoding_type OTHER, not %d", _currently_decoding_type);
        return false;
    }
    _currently_decoding_type = CBE_DECODING_STRING;
    _currently_decoding_length = byte_count;
    _currently_decoding_offset = 0;
    _currently_decoding_data.clear();
    return true;
}

bool decoder::on_bytes_begin(int64_t byte_count)
{
    if(!_forced_callback_return_value) return false;

    if(_currently_decoding_type != CBE_DECODING_OTHER)
    {
        KSLOG_ERROR("Expected _currently_decoding_type OTHER, not %d", _currently_decoding_type);
        return false;
    }
    _currently_decoding_type = CBE_DECODING_BYTES;
    _currently_decoding_length = byte_count;
    _currently_decoding_offset = 0;
    _currently_decoding_data.clear();
    return true;
}

bool decoder::on_uri_begin(int64_t byte_count)
{
    if(!_forced_callback_return_value) return false;

    if(_currently_decoding_type != CBE_DECODING_OTHER)
    {
        KSLOG_ERROR("Expected _currently_decoding_type OTHER, not %d", _currently_decoding_type);
        return false;
    }
    _currently_decoding_type = CBE_DECODING_URI;
    _currently_decoding_length = byte_count;
    _currently_decoding_offset = 0;
    _currently_decoding_data.clear();
    return true;
}

bool decoder::on_comment_begin(int64_t byte_count)
{
    if(!_forced_callback_return_value) return false;

    if(_currently_decoding_type != CBE_DECODING_OTHER)
    {
        KSLOG_ERROR("Expected _currently_decoding_type OTHER, not %d", _currently_decoding_type);
        return false;
    }
    _currently_decoding_type = CBE_DECODING_COMMENT;
    _currently_decoding_length = byte_count;
    _currently_decoding_offset = 0;
    _currently_decoding_data.clear();
    return true;
}

bool decoder::on_array_data(const uint8_t* start, int64_t byte_count)
{
    if(!_forced_callback_return_value) return false;

    KSLOG_DEBUG("Add array data %d bytes", byte_count);
    if(_currently_decoding_type == CBE_DECODING_OTHER)
    {
        KSLOG_ERROR("Expecting array decoding type but have OTHER");
        return false;
    }

    if(_currently_decoding_offset + byte_count > _currently_decoding_length)
    {
        KSLOG_ERROR("_currently_decoding_offset + byte_count (%d) > _currently_decoding_length (%d)",
            _currently_decoding_offset + byte_count, _currently_decoding_length);
        return false;
    }

    _currently_decoding_data.insert(_currently_decoding_data.end(), start, start+byte_count);
    _currently_decoding_offset += byte_count;

    if(_currently_decoding_offset == _currently_decoding_length)
    {
        cbe_decoding_type decoding_type = _currently_decoding_type;
        _currently_decoding_type = CBE_DECODING_OTHER;
        switch(decoding_type)
        {
            case CBE_DECODING_BYTES:
                return mark_complete(encoding::value::binv(_currently_decoding_data));
            case CBE_DECODING_URI:
                return mark_complete(encoding::value::uriv(std::string(_currently_decoding_data.begin(), _currently_decoding_data.end())));
            case CBE_DECODING_COMMENT:
                return mark_complete(encoding::value::comv(std::string(_currently_decoding_data.begin(), _currently_decoding_data.end())));
            case CBE_DECODING_STRING:
                return mark_complete(encoding::value::strv((std::string(_currently_decoding_data.begin(), _currently_decoding_data.end()))));
            default:
                KSLOG_ERROR("Unhandled decoding type: %d", decoding_type);
                return false;
        }
    }

    return true;
}
