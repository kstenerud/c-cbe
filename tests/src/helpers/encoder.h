#pragma once

#include <functional>
#include <cbe/cbe.h>
#include "encoding.h"

class encoder
{
private:
    std::vector<char> _process_backing_store;
    cbe_encode_process* _process;
    std::vector<uint8_t> _buffer;
    std::vector<uint8_t> _encoded_data;
    int _max_container_depth;
    std::function<bool(uint8_t* data_start, int64_t length)> _on_data_ready;

private:
    bool flush_buffer();
    cbe_encode_status flush_and_retry(std::function<cbe_encode_status()> my_function);
    cbe_encode_status encode(const encoding::value& v);
    cbe_encode_status stream_array(const std::vector<uint8_t>& data);

public:
    encoder(int64_t buffer_size,
            int max_container_depth,
            std::function<bool(uint8_t* data_start, int64_t length)> on_data_ready =
                [](uint8_t* data_start, int64_t length)
                {(void)data_start; (void)length; return true;});

    // Encode an encoding object and all linked objects.
    cbe_encode_status encode(const encoding::enc& enc);

    // Encode entire array objects. If the entire object won't fit,
    // returns with failure.
    cbe_encode_status encode_string(std::vector<uint8_t> value);
    cbe_encode_status encode_bytes(std::vector<uint8_t> value);
    cbe_encode_status encode_uri(std::vector<uint8_t> value);
    cbe_encode_status encode_comment(std::vector<uint8_t> value);

    int64_t get_encode_buffer_offset() const;

    // Get the complete raw encoded data.
    std::vector<uint8_t>& encoded_data() {return _encoded_data;}
};
