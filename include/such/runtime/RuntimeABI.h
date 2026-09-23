#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SUCH_RUNTIME_ABI_VERSION 1u

typedef void* such_runtime_handle_v1;

typedef struct such_runtime_result_v1 {
    uint64_t file_id;
    const char* filename;
    const char* path;
    const char* extension;
    int32_t pinned;
    int32_t indexed;
    int32_t authorized;
    int64_t modified_unix_seconds;
    uint64_t size_bytes;
} such_runtime_result_v1;

typedef struct such_runtime_status_v1 {
    int32_t indexing;
    uint64_t indexed_files;
    uint64_t generation;
} such_runtime_status_v1;

typedef struct such_runtime_engine_stats_v1 {
    uint64_t rows;
    uint64_t chunks;
    uint64_t candidate_chunks;
    uint64_t candidate_rows;
    uint64_t pruned_by_time;
    uint64_t pruned_by_extension;
    uint64_t pruned_by_text;
    uint64_t workers_used;
    int32_t parallel;
    int32_t backend_loaded;
    int32_t backend_active;
    uint64_t backend_candidate_rows;
    uint64_t dynamic_queries;
    uint64_t dynamic_tiles;
    uint64_t direct_compaction_queries;
} such_runtime_engine_stats_v1;

// Optional ABI-v1 extension introduced by SuchRuntimePrivate v0.7.0. The base
// ABI version remains 1 so older runtimes continue to support file search.
typedef enum such_runtime_content_locator_kind_v1 {
    SUCH_CONTENT_LOCATOR_NONE = 0,
    SUCH_CONTENT_LOCATOR_LINE = 1,
    SUCH_CONTENT_LOCATOR_PAGE = 2,
    SUCH_CONTENT_LOCATOR_SLIDE = 3,
    SUCH_CONTENT_LOCATOR_SHEET = 4,
    SUCH_CONTENT_LOCATOR_OFFSET = 5,
    SUCH_CONTENT_LOCATOR_METADATA = 6,
} such_runtime_content_locator_kind_v1;

typedef struct such_runtime_content_search_request_v1 {
    const char* query_utf8;
    const uint64_t* candidate_file_ids;
    size_t candidate_file_count;
    size_t max_results;
    uint32_t flags;
} such_runtime_content_search_request_v1;

typedef struct such_runtime_content_result_v1 {
    uint64_t file_id;
    const char* filename;
    const char* path;
    const char* extension;
    uint32_t locator_kind;
    uint32_t page_number;
    uint32_t line_number;
    uint32_t slide_number;
    uint32_t sheet_number;
    uint64_t byte_offset;
    const char* logical_name;
    const char* snippet;
    double score;
} such_runtime_content_result_v1;

typedef int (*such_runtime_string_callback_v1)(void* user, const char* value);
typedef int (*such_runtime_result_callback_v1)(void* user, const such_runtime_result_v1* value);
typedef int (*such_runtime_content_result_callback_v1)(void* user, const such_runtime_content_result_v1* value);

uint32_t such_runtime_abi_version(void);
such_runtime_handle_v1 such_runtime_create_v1(const char* state_directory_utf8);
void such_runtime_destroy_v1(such_runtime_handle_v1 handle);
int such_runtime_load_v1(such_runtime_handle_v1 handle);
const char* such_runtime_last_error_v1(such_runtime_handle_v1 handle);
int such_runtime_add_root_v1(such_runtime_handle_v1 handle, const char* path_utf8);
int such_runtime_remove_root_v1(such_runtime_handle_v1 handle, const char* path_utf8);
int such_runtime_replace_roots_v1(such_runtime_handle_v1 handle, const char* const* paths_utf8, size_t count);
int such_runtime_list_roots_v1(such_runtime_handle_v1 handle, such_runtime_string_callback_v1 callback, void* user);
int such_runtime_list_extensions_v1(such_runtime_handle_v1 handle, such_runtime_string_callback_v1 callback, void* user);
void such_runtime_reindex_async_v1(such_runtime_handle_v1 handle);
void such_runtime_wait_for_idle_v1(such_runtime_handle_v1 handle);
int such_runtime_search_v1(
    such_runtime_handle_v1 handle,
    const char* query_utf8,
    uint32_t dialect,
    size_t max_results,
    such_runtime_result_callback_v1 callback,
    void* user);

// Optional: callers must discover this symbol dynamically. Do not make it a
// mandatory load dependency for the base v1 runtime contract.
int such_runtime_search_content_v1(
    such_runtime_handle_v1 handle,
    const such_runtime_content_search_request_v1* request,
    such_runtime_content_result_callback_v1 callback,
    void* user);

int such_runtime_set_pinned_v1(such_runtime_handle_v1 handle, const char* path_utf8, int pinned);
int such_runtime_set_indexed_v1(such_runtime_handle_v1 handle, const char* path_utf8, int indexed);
int such_runtime_status_v1_fn(such_runtime_handle_v1 handle, such_runtime_status_v1* out_status);
int such_runtime_engine_stats_v1_fn(such_runtime_handle_v1 handle, such_runtime_engine_stats_v1* out_stats);
uint64_t such_runtime_generation_v1(such_runtime_handle_v1 handle);

#ifdef __cplusplus
}
#endif
