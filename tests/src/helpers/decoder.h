#pragma once

#include <functional>
#include <vector>
#include <cbe/cbe.h>
#include "encoding.h"

typedef enum
{
    CBE_DECODING_OTHER,
    CBE_DECODING_STRING,
    CBE_DECODING_BYTES,
    CBE_DECODING_URI,
    CBE_DECODING_COMMENT,
} cbe_decoding_type;

class decoder
{
private:
    std::vector<char> _process_backing_store;
    cbe_decode_process* _process;
    std::vector<uint8_t> _received_data;
    int64_t _read_offset = 0;
    bool _process_is_valid = true;
    encoding::enc _enc;
    cbe_decoding_type _currently_decoding_type = CBE_DECODING_OTHER;
    int64_t _currently_decoding_length;
    int64_t _currently_decoding_offset;
    std::vector<uint8_t> _currently_decoding_data;
    bool _forced_callback_return_value;
    int _max_container_depth;

    bool mark_complete(encoding::value v);

public:
    // Internal callback functions
    bool on_nil();
    bool on_boolean(bool value);
    bool on_integer(int sign, uint64_t value);
    bool on_float(double value);
    bool on_decimal_float(dec64_ct value);
    bool on_list_begin();
    bool on_unordered_map_begin();
    bool on_ordered_map_begin();
    bool on_metadata_map_begin();
    bool on_container_end();
    bool on_string_begin(int64_t byte_count);
    bool on_bytes_begin(int64_t byte_count);
    bool on_uri_begin(int64_t byte_count);
    bool on_comment_begin(int64_t byte_count);
    bool on_array_data(const uint8_t* start, int64_t byte_count);

public:
    decoder(int max_container_depth, bool forced_callback_return_value);

    // Begin the decoding process.
    cbe_decode_status begin();

    // Feed data to be decoded.
    cbe_decode_status feed(const std::vector<uint8_t>& data);

    // End the decoding process.
    cbe_decode_status end();

    // Decode an entire document
    cbe_decode_status decode(const std::vector<uint8_t>& document);

    // Get the complete raw data received.
    std::vector<uint8_t>& received_data();

    // Get the decoded encoding objects.
    encoding::enc decoded();
};
