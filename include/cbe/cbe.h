#pragma once

#ifndef CBE_PUBLIC
    #if defined _WIN32 || defined __CYGWIN__
        #define CBE_PUBLIC __declspec(dllimport)
    #else
        #define CBE_PUBLIC
    #endif
#endif

#ifndef ANSI_EXTENSION
    #ifdef __GNUC__
        #define ANSI_EXTENSION __extension__
    #else
        #define ANSI_EXTENSION
    #endif
#endif

#ifdef __cplusplus
// Use the same type names as C
#include <decimal/decimal>
ANSI_EXTENSION typedef std::decimal::decimal32::__decfloat32   dec32_ct;
ANSI_EXTENSION typedef std::decimal::decimal64::__decfloat64   dec64_ct;
ANSI_EXTENSION typedef std::decimal::decimal128::__decfloat128 dec128_ct;
#else
    ANSI_EXTENSION typedef _Decimal32 dec32_ct;
    ANSI_EXTENSION typedef _Decimal64 dec64_ct;
    ANSI_EXTENSION typedef _Decimal128 dec128_ct;
#endif
ANSI_EXTENSION typedef __int128 int128_ct;
ANSI_EXTENSION typedef unsigned __int128 uint128_ct;
ANSI_EXTENSION typedef __float128 float128_ct;

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <smalltime/smalltime.h>
#include <smalltime/nanotime.h>



// ========
// Defaults
// ========

#ifndef CBE_DEFAULT_MAX_CONTAINER_DEPTH
    #define CBE_DEFAULT_MAX_CONTAINER_DEPTH 500
#endif


// -----------
// Library API
// -----------

/**
 * Get the current library version as a semantic version (e.g. "1.5.2").
 *
 * @return The library version.
 */
CBE_PUBLIC const char* cbe_version();



// ------------
// Decoding API
// ------------

struct cbe_decode_process;

/**
 * Status codes that can be returned by decoder functions.
 */
typedef enum
{
    /**
     * The document has been successfully decoded.
     */
    CBE_DECODE_STATUS_OK = 0,

    /**
     * The decoder has reached the end of the buffer and needs more data to
     * finish decoding the document.
     */
    CBE_DECODE_STATUS_NEED_MORE_DATA = 100,

    /**
     * A user callback returned false, stopping the decode process.
     * The process may be resumed after fixing whatever problem caused the
     * callback to return false.
     */
    CBE_DECODE_STATUS_STOPPED_IN_CALLBACK,

    /**
     * One or more of the arguments was invalid.
     */
    CBE_DECODE_ERROR_INVALID_ARGUMENT,

    /**
     * The array data was invalid.
     */
    CBE_DECODE_ERROR_INVALID_ARRAY_DATA,

    /**
     * Unbalanced list/map begin and end markers were detected.
     */
    CBE_DECODE_ERROR_UNBALANCED_CONTAINERS,

    /**
     * An invalid data type was used as a map key.
     */
    CBE_DECODE_ERROR_INCORRECT_MAP_KEY_TYPE,

    /**
     * A map contained a key with no value.
     */
    CBE_DECODE_ERROR_MAP_MISSING_VALUE_FOR_KEY,

    /**
     * An array field was not completed before ending the decode process.
     */
    CBE_DECODE_ERROR_INCOMPLETE_ARRAY_FIELD,

    /**
     * The currently open field is smaller than the data being added.
     */
    CBE_DECODE_ERROR_ARRAY_FIELD_LENGTH_EXCEEDED,

    /**
     * Max container depth (default 500) was exceeded.
     */
    CBE_DECODE_ERROR_MAX_CONTAINER_DEPTH_EXCEEDED,

    /**
     * An internal bug triggered an error.
     */
    CBE_DECODE_ERROR_INTERNAL_BUG,

} cbe_decode_status;

/**
 * Callbacks for use with cbe_decode(). These mirror the encode API.
 *
 * cbe_decode() will call these callbacks as it decodes objects in the document.
 *
 * Callback functions return true if processing should continue.
 */
typedef struct
{
    // A nil value was decoded.
    bool (*on_nil) (struct cbe_decode_process* decode_process);

    // An boolean value was decoded.
    bool (*on_boolean) (struct cbe_decode_process* decode_process, bool value);

    // An integer value was decoded. Sign will be 1 or -1.
    bool (*on_integer) (struct cbe_decode_process* decode_process, int sign, uint64_t value);

    // A binary floating point value was decoded.
    bool (*on_float) (struct cbe_decode_process* decode_process, double value);

    // A decimal floating point value was decoded.
    // bool (*on_decimal) (struct cbe_decode_process* decode_process, dec64_ct value);

    // A date value was decoded.
//    bool (*on_date) (struct cbe_decode_process* decode_process, int year, int month, int day);

    // A time value was decoded.
//    bool (*on_time) (struct cbe_decode_process* decode_process, int hour, int minute, int second, int nanosecond, const char* timezone);

    // A timestamp value was decoded.
//    bool (*on_timestamp) (struct cbe_decode_process* decode_process, int year, int month, int day, int hour, int minute, int second, int nanosecond, const char* timezone);

    // A list has been opened.
    bool (*on_list_begin) (struct cbe_decode_process* decode_process);

    // An unordered map has been opened.
    bool (*on_unordered_map_begin) (struct cbe_decode_process* decode_process);

    // An ordered map has been opened.
    bool (*on_ordered_map_begin) (struct cbe_decode_process* decode_process);

    // A metadata map has been opened.
    bool (*on_metadata_map_begin) (struct cbe_decode_process* decode_process);

    // The current container has been closed.
    bool (*on_container_end) (struct cbe_decode_process* decode_process);

    /**
     * A string array has been opened. Expect subsequent calls to
     * on_string_data() until the array has been filled. Once `byte_count`
     * bytes have been added via on_string_data(), the field is considered
     * "complete" and is closed.
     *
     * @param decode_process The decode process.
     * @param byte_count The total length of the string.
     */
    bool (*on_string_begin) (struct cbe_decode_process* decode_process, int64_t byte_count);

    /**
     * A byte array has been opened. Expect subsequent calls to
     * on_bytes_data() until the array has been filled. Once `byte_count`
     * bytes have been added via on_bytes_data(), the field is considered
     * "complete" and is closed.
     *
     * @param decode_process The decode process.
     * @param byte_count The total length of the array.
     */
    bool (*on_bytes_begin) (struct cbe_decode_process* decode_process, int64_t byte_count);

    /**
     * A URI array has been opened. Expect subsequent calls to
     * on_uri_data() until the array has been filled. Once `byte_count`
     * bytes have been added via on_uri_data(), the field is considered
     * "complete" and is closed.
     *
     * @param decode_process The decode process.
     * @param byte_count The total length of the URI.
     */
    bool (*on_uri_begin) (struct cbe_decode_process* decode_process, int64_t byte_count);

    /**
     * A comment array has been opened. Expect subsequent calls to
     * on_comment_data() until the array has been filled. Once `byte_count`
     * bytes have been added via on_comment_data(), the field is considered
     * "complete" and is closed.
     *
     * @param decode_process The decode process.
     * @param byte_count The total length of the comment.
     */
    bool (*on_comment_begin) (struct cbe_decode_process* decode_process, int64_t byte_count);

    /**
     * Array data was decoded, and should be added to the current array field.
     *
     * @param decode_process The decode process.
     * @param start The start of the data.
     * @param byte_count The number of bytes in this array fragment.
     */
    bool (*on_array_data) (struct cbe_decode_process* decode_process,
                             const uint8_t* start,
                             int64_t byte_count);
} cbe_decode_callbacks;


// ------------------
// Decoder Simple API
// ------------------

/**
 * Get the user context information from a decode process.
 * This is meant to be called by a decode callback function.
 *
 * @param decode_process The decode process.
 * @return The user context.
 */
CBE_PUBLIC void* cbe_decode_get_user_context(struct cbe_decode_process* decode_process);

/**
 * Decode an entire CBE document.
 *
 * @param callbacks The callbacks to call while decoding the document.
 * @param user_context Whatever data you want to be available to the callbacks.
 * @param document_start The start of the document.
 * @param byte_count The number of bytes in the document.
 * @param max_container_depth The maximum container depth to suppport (<=0 means use default).
 */
CBE_PUBLIC cbe_decode_status cbe_decode(const cbe_decode_callbacks* callbacks,
                                        void* user_context,
                                        const uint8_t* document_start,
                                        int64_t byte_count,
                                        int max_container_depth);



// -----------------
// Decoder Power API
// -----------------

/**
 * Get the size of the decode process data.
 * Use this to create a backing store for the process data like so:
 *     char process_backing_store[cbe_decode_process_size(max_depth)];
 *     struct cbe_decode_process* decode_process = (struct cbe_decode_process*)process_backing_store;
 * or
 *     struct cbe_decode_process* decode_process = (struct cbe_decode_process*)malloc(cbe_decode_process_size());
 * or
 *     std::vector<char> process_backing_store(cbe_decode_process_size(max_depth));
 *     struct cbe_decode_process* decode_process = (struct cbe_decode_process*)process_backing_store.data();
 *
 * @param max_container_depth The maximum container depth to suppport (<=0 means use default).
 * @return The process data size.
 */
CBE_PUBLIC int cbe_decode_process_size(int max_container_depth);

/**
 * Begin a new decoding process.
 *
 * @param decode_process The decode process to initialize.
 * @param callbacks The callbacks to call while decoding the document.
 * @param user_context Whatever data you want to be available to the callbacks.
 * @param max_container_depth The maximum container depth to suppport (<=0 means use default).
 * @return The current decoder status.
 */
CBE_PUBLIC cbe_decode_status cbe_decode_begin(struct cbe_decode_process* decode_process,
                                              const cbe_decode_callbacks* callbacks,
                                              void* user_context,
                                              int max_container_depth);

/**
 * Decode part of a CBE document.
 *
 * Encountering CBE_DECODE_STATUS_NEED_MORE_DATA means that cbe_decode_feed()
 * has decoded everything it can from the current buffer, and is ready to
 * receive the next buffer of encoded data. THE PROCESS MAY NOT HAVE CONSUMED
 * ALL BYTES IN THE BUFFER. You must read the output value in byte_count to see
 * how many bytes were consumed, move the unconsumed bytes to the beginning of
 * the new buffer, add more bytes after that, then call cbe_decode_feed() again.
 *
 * @param decode_process The decode process.
 * @param data_start The start of the document.
 * @param byte_count In: The length of the data in bytes. Out: Number of bytes consumed.
 * @return The current decoder status.
 */
CBE_PUBLIC cbe_decode_status cbe_decode_feed(struct cbe_decode_process* decode_process,
                                             const uint8_t* data_start,
                                             int64_t* byte_count);

/**
 * Get the current offset into the overall stream of data.
 * This is the total bytes read across all calls to cbe_decode_feed().
 *
 * @param decode_process The decode process.
 * @return The current offset.
 */
CBE_PUBLIC int64_t cbe_decode_get_stream_offset(struct cbe_decode_process* decode_process);

/**
 * End a decoding process, checking for document validity.
 *
 * @param decode_process The decode process.
 * @return The final decoder status.
 */
CBE_PUBLIC cbe_decode_status cbe_decode_end(struct cbe_decode_process* decode_process);


// ------------
// Encoding API
// ------------

struct cbe_encode_process;

/**
 * Status codes that can be returned by encoder functions.
 */
typedef enum
{
    /**
     * Completed successfully.
     */
    CBE_ENCODE_STATUS_OK = 0,

    /**
     * The encoder has reached the end of the buffer and needs more room to
     * encode this object.
     */
    CBE_ENCODE_STATUS_NEED_MORE_ROOM = 200,

    /**
     * One or more of the arguments was invalid.
     */
    CBE_ENCODE_ERROR_INVALID_ARGUMENT,

    /**
     * The array data was invalid.
     */
    CBE_ENCODE_ERROR_INVALID_ARRAY_DATA,

    /**
     * Unbalanced list/map begin and end markers were detected.
     */
    CBE_ENCODE_ERROR_UNBALANCED_CONTAINERS,

    /**
     * An invalid data type was used as a map key.
     */
    CBE_ENCODE_ERROR_INCORRECT_MAP_KEY_TYPE,

    /**
     * A map contained a key with no value.
     */
    CBE_ENCODE_ERROR_MAP_MISSING_VALUE_FOR_KEY,

    /**
     * Attempted to add a new array field or close the document before the
     * currently open field was completely filled.
     */
    CBE_ENCODE_ERROR_INCOMPLETE_ARRAY_FIELD,

    /**
     * The currently open field is smaller than the data being added.
     */
    CBE_ENCODE_ERROR_ARRAY_FIELD_LENGTH_EXCEEDED,

    /**
     * We're not inside an array field.
     */
    CBE_ENCODE_ERROR_NOT_INSIDE_ARRAY_FIELD,

    /**
     * Max container depth (default 500) was exceeded.
     */
    CBE_ENCODE_ERROR_MAX_CONTAINER_DEPTH_EXCEEDED,

} cbe_encode_status;


/**
 * Get the size of the encode process data.
 * Use this to create a backing store for the process data like so:
 *     char process_backing_store[cbe_encode_process_size()];
 *     struct cbe_encode_process* encode_process = (struct cbe_encode_process*)process_backing_store;
 * or
 *     struct cbe_encode_process* encode_process = (struct cbe_encode_process*)malloc(cbe_encode_process_size());
 * or
 *     std::vector<char> process_backing_store(cbe_encode_process_size());
 *     struct cbe_encode_process* encode_process = (struct cbe_encode_process*)process_backing_store.data();
 *
 * @param max_container_depth The maximum container depth to suppport (<=0 means use default).
 * @return The process data size.
 */
CBE_PUBLIC int cbe_encode_process_size(int max_container_depth);

/**
 * Begin a new encoding process.
 *
 * @param encode_process The encode process to initialize.
 * @param document_buffer A buffer to store the document in.
 * @param byte_count Size of the buffer in bytes.
 * @param max_container_depth The maximum container depth to suppport (<=0 means use default).
 * @return The current encoder status.
 */
CBE_PUBLIC cbe_encode_status cbe_encode_begin(struct cbe_encode_process* encode_process,
                                              uint8_t* document_buffer,
                                              int64_t byte_count,
                                              int max_container_depth);

/**
 * Replace the document buffer in an encode process.
 * This also resets the buffer offset.
 *
 * @param encode_process The encode process.
 * @param document_buffer A buffer to store the document in.
 * @param byte_count Size of the buffer in bytes.
 * @return The current encoder status.
 */
CBE_PUBLIC cbe_encode_status cbe_encode_set_buffer(struct cbe_encode_process* encode_process,
                                                   uint8_t* document_buffer,
                                                   int64_t byte_count);

/**
 * Get the current write offset into the encode buffer.
 * This points to one past the last byte written to the current buffer.
 *
 * When a function returns CBE_ENCODE_STATUS_NEED_MORE_ROOM, you'll need to
 * flush the buffer up to this point, then call cbe_encode_set_buffer() again.
 *
 * @param encode_process The encode process.
 * @return The current offset.
 */
CBE_PUBLIC int64_t cbe_encode_get_buffer_offset(struct cbe_encode_process* encode_process);

/**
 * Get the document depth. This is the total depth of lists or maps that
 * haven't yet been terminated with an "end container". It must be 0 in order
 * to successfully complete a document.
 *
 * A negative value indicates that you've added too many "end container"
 * objects, and the document is now invalid. Note: Adding more containers
 * to "balance it out" won't help; your code has a bug, and must be fixed.
 *
 * @param encode_process The encode process.
 * @return the document depth.
 */
CBE_PUBLIC int cbe_encode_get_document_depth(struct cbe_encode_process* encode_process);

/**
 * End an encoding process, checking the document for validity.
 *
 * @param encode_process The encode process.
 * @return The final encoder status.
 */
CBE_PUBLIC cbe_encode_status cbe_encode_end(struct cbe_encode_process* encode_process);

/**
 * Add padding to the document.
 *
 * @param encode_process The encode process.
 * @param byte_count The number of bytes of padding to add.
 * @return The current encoder status.
 */
CBE_PUBLIC cbe_encode_status cbe_encode_add_padding(struct cbe_encode_process* encode_process,
                                                    int byte_count);

/**
 * Add a nil object to the document.
 *
 * @param encode_process The encode process.
 * @return The current encoder status.
 */
CBE_PUBLIC cbe_encode_status cbe_encode_add_nil(struct cbe_encode_process* encode_process);

/**
 * Add a boolean value to the document.
 *
 * @param encode_process The encode process.
 * @param value The value to add.
 * @return The current encoder status.
 */
CBE_PUBLIC cbe_encode_status cbe_encode_add_boolean(struct cbe_encode_process* encode_process, bool value);

/**
 * Add an integer value to the document.
 *
 * @param encode_process The encode process.
 * @param value The value to add.
 * @return The current encoder status.
 */
CBE_PUBLIC cbe_encode_status cbe_encode_add_integer(struct cbe_encode_process* encode_process, int sign, uint64_t value);

/**
 * Add a floating point value to the document.
 * Note that this will add a narrower type if it will fit.
 *
 * @param encode_process The encode process.
 * @param value The value to add.
 * @return The current encoder status.
 */
CBE_PUBLIC cbe_encode_status cbe_encode_add_float(struct cbe_encode_process* encode_process, double value);

/**
 * Begin a list in the document. Must be matched by an end container.
 *
 * @param encode_process The encode process.
 * @return The current encoder status.
 */
CBE_PUBLIC cbe_encode_status cbe_encode_list_begin(struct cbe_encode_process* encode_process);

/**
 * Begin an unordered map in the document. Must be matched by an end container.
 *
 * Map entries must be added in pairs. Every even item is a key, and every
 * odd item is a corresponding value.
 *
 * @param encode_process The encode process.
 * @return The current encoder status.
 */
CBE_PUBLIC cbe_encode_status cbe_encode_unordered_map_begin(struct cbe_encode_process* encode_process);

/**
 * Begin an ordered map in the document. Must be matched by an end container.
 *
 * Map entries must be added in pairs. Every even item is a key, and every
 * odd item is a corresponding value.
 *
 * @param encode_process The encode process.
 * @return The current encoder status.
 */
CBE_PUBLIC cbe_encode_status cbe_encode_ordered_map_begin(struct cbe_encode_process* encode_process);

/**
 * Begin a metadata map in the document. Must be matched by an end container.
 *
 * Map entries must be added in pairs. Every even item is a key, and every
 * odd item is a corresponding value.
 *
 * @param encode_process The encode process.
 * @return The current encoder status.
 */
CBE_PUBLIC cbe_encode_status cbe_encode_metadata_map_begin(struct cbe_encode_process* encode_process);

/**
 * End the current container (list or map) in the document.
 * If calling this function would result in too many end containers,
 * the operation is aborted and returns CBE_ENCODE_ERROR_UNBALANCED_CONTAINERS
 *
 * @param encode_process The encode process.
 * @return The current encoder status.
 */
CBE_PUBLIC cbe_encode_status cbe_encode_container_end(struct cbe_encode_process* encode_process);

/**
 * Convenience function: add a UTF-8 encoded string and its data to a document.
 * This function does not preserve partial data in the encoded buffer. Either the
 * entire operation succeeds, or it fails, restoring the buffer offset to where it
 * was before adding the array header.
 * Note: Do not include a byte order marker (BOM).
 *
 * @param encode_process The encode process.
 * @param string_start The string to add. May be NULL iff byte_count = 0.
 * @param byte_count The number of bytes in the string.
 * @return The current encoder status.
 */
CBE_PUBLIC cbe_encode_status cbe_encode_add_string(struct cbe_encode_process* encode_process,
                                                   const char* string_start,
                                                   int64_t byte_count);

/**
 * Convenience function: add a byte array to a document.
 * This function does not preserve partial data in the encoded buffer. Either the
 * entire operation succeeds, or it fails, restoring the buffer offset to where it
 * was before adding the array header.
 *
 * @param encode_process The encode process.
 * @param data The data to add. May be NULL iff byte_count = 0.
 * @return The current encoder status.
 */
CBE_PUBLIC cbe_encode_status cbe_encode_add_bytes(struct cbe_encode_process* encode_process,
                                                   const uint8_t* data,
                                                   int64_t byte_count);

/**
 * Convenience function: add a URI to a document.
 * This function does not preserve partial data in the encoded buffer. Either the
 * entire operation succeeds, or it fails, restoring the buffer offset to where it
 * was before adding the array header.
 *
 * Note: This library performs no validation of the contents of the URI. Use a proper
 *       RFC 3986 validation library for any URIs.
 *
 * @param encode_process The encode process.
 * @param uri_start The uri to add. May be NULL iff byte_count = 0.
 * @param byte_count The number of bytes in the string.
 * @return The current encoder status.
 */
CBE_PUBLIC cbe_encode_status cbe_encode_add_uri(struct cbe_encode_process* encode_process,
                                                const char* uri_start,
                                                int64_t byte_count);

/**
 * Convenience function: add a UTF-8 encoded comment and its data to a document.
 * This function does not preserve partial data in the encoded buffer. Either the
 * entire operation succeeds, or it fails, restoring the buffer offset to where it
 * was before adding the array header.
 * Note: Do not include a byte order marker (BOM).
 *
 * @param encode_process The encode process.
 * @param comment_start The comment to add. May be NULL iff byte_count = 0.
 * @param byte_count The number of bytes in the string.
 * @return The current encoder status.
 */
CBE_PUBLIC cbe_encode_status cbe_encode_add_comment(struct cbe_encode_process* encode_process,
                                                    const char* comment_start,
                                                    int64_t byte_count);

/**
 * Begin a string in the document. The string data will be UTF-8 without a BOM.
 *
 * This function "opens" a string field, encoding the type and length portions.
 * The encode process will expect subsequent cbe_encode_add_data() calls
 * to fill up the field.
 * Once the field has been filled, it is considered "closed", and other fields
 * may now be added to the document. A zero-length array is automatically closed
 * in this function.
 *
 * @param encode_process The encode process.
 * @param byte_count The total length of the string to add in bytes.
 * @return The current encoder status.
 */
CBE_PUBLIC cbe_encode_status cbe_encode_string_begin(struct cbe_encode_process* encode_process, int64_t byte_count);

/**
 * Begin an array of bytes in the document.
 *
 * This function "opens" a byte array field, encoding the type and length portions.
 * The encode process will expect subsequent cbe_encode_add_bytes_data() calls
 * to fill up the field.
 * Once the field has been filled, it is considered "closed", and other fields
 * may now be added to the document. A zero-length array is automatically closed
 * in this function.
 *
 * @param encode_process The encode process.
 * @param byte_count The total length of the data to add in bytes.
 * @return The current encoder status.
 */
CBE_PUBLIC cbe_encode_status cbe_encode_bytes_begin(struct cbe_encode_process* encode_process, int64_t byte_count);

/**
 * Begin a URI in the document. The URI must conform to RFC 3986.
 *
 * This function "opens" a URI field, encoding the type and length portions.
 * The encode process will expect subsequent cbe_encode_add_data() calls
 * to fill up the field.
 * Once the field has been filled, it is considered "closed", and other fields
 * may now be added to the document. A zero-length array is automatically closed
 * in this function.
 *
 * Note: This library performs no validation of the contents of the URI. Use a proper
 *       RFC 3986 validation library for any URIs.
 *
 * @param encode_process The encode process.
 * @param byte_count The total length of the URI to add in bytes.
 * @return The current encoder status.
 */
CBE_PUBLIC cbe_encode_status cbe_encode_uri_begin(struct cbe_encode_process* encode_process, int64_t byte_count);

/**
 * Begin a comment in the document. The comment data will be UTF-8 without a BOM.
 *
 * This function "opens" a comment field, encoding the type and length portions.
 * The encode process will expect subsequent cbe_encode_add_data() calls
 * to fill up the field.
 * Once the field has been filled, it is considered "closed", and other fields
 * may now be added to the document. A zero-length array is automatically closed
 * in this function.
 *
 * @param encode_process The encode process.
 * @param byte_count The total length of the comment to add in bytes.
 * @return The current encoder status.
 */
CBE_PUBLIC cbe_encode_status cbe_encode_comment_begin(struct cbe_encode_process* encode_process, int64_t byte_count);

/**
 * Add data to the currently opened array field (string, bytes, uri, comment).
 *
 * If there's not enough room in the buffer, this function will add as much
 * data as it can to the encoded buffer before setting the output value of
 * byte_count to the number of bytes consumed, and then returning
 * CBE_ENCODE_STATUS_NEED_MORE_ROOM.
 *
 * Once enough bytes have been consumed to fill the array's data, the field
 * is considered "completed", and is closed.
 *
 * Note: You don't need to call this function for zero-length arrays. Calling
 *       with *byte_count == 0 does nothing and returns CBE_ENCODE_STATUS_OK.
 *
 * @param encode_process The encode process.
 * @param start The start of the data to add. May be NULL iff *byte_count = 0.
 * @param byte_count In: The length of the data in bytes. Out: Number of bytes consumed.
 * @return The current encoder status.
 */
CBE_PUBLIC cbe_encode_status cbe_encode_add_data(struct cbe_encode_process* encode_process,
                                                 const uint8_t* start,
                                                 int64_t* byte_count);



#ifdef __cplusplus 
}
#endif
