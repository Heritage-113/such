#include <cstdint>
#include <cstdlib>
#include <such/runtime/RuntimeABI.h>
#include <string>
#include <vector>

namespace {
struct Stub { std::string error; std::vector<std::string> roots; std::uint64_t generation = 1; };
Stub* ptr(such_runtime_handle_v1 h) { return static_cast<Stub*>(h); }

bool env_flag_enabled(const char* name) {
#if defined(_WIN32)
    char* buffer = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&buffer, &length, name) != 0 || buffer == nullptr) return false;
    const bool enabled = *buffer != '\0';
    std::free(buffer);
    return enabled;
#else
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0';
#endif
}
}
extern "C" {
uint32_t such_runtime_abi_version(void) { return SUCH_RUNTIME_ABI_VERSION; }
such_runtime_handle_v1 such_runtime_create_v1(const char*) { return new Stub{}; }
void such_runtime_destroy_v1(such_runtime_handle_v1 h) { delete ptr(h); }
int such_runtime_load_v1(such_runtime_handle_v1 h) {
    if (!h) return 0;
    if (env_flag_enabled("SUCH_FAKE_RUNTIME_FAIL_LOAD")) {
        ptr(h)->error = "forced fake runtime load failure";
        return 0;
    }
    return 1;
}
const char* such_runtime_last_error_v1(such_runtime_handle_v1 h) { return h != nullptr ? ptr(h)->error.c_str() : "invalid handle"; }
int such_runtime_add_root_v1(such_runtime_handle_v1 h, const char* p) { if (!h || !p) return 0; ptr(h)->roots.emplace_back(p); ++ptr(h)->generation; return 1; }
int such_runtime_remove_root_v1(such_runtime_handle_v1 h, const char*) { if (!h) return 0; ptr(h)->roots.clear(); ++ptr(h)->generation; return 1; }
int such_runtime_replace_roots_v1(such_runtime_handle_v1 h, const char* const* p, size_t n) { if (!h) return 0; ptr(h)->roots.clear(); for (size_t i=0;i<n;++i) if (p[i]) ptr(h)->roots.emplace_back(p[i]); ++ptr(h)->generation; return 1; }
int such_runtime_list_roots_v1(such_runtime_handle_v1 h, such_runtime_string_callback_v1 cb, void* u) {
    if (!h || !cb) return 0;
    if (env_flag_enabled("SUCH_FAKE_RUNTIME_FAIL_ROOTS")) {
        ptr(h)->error = "forced fake runtime roots failure";
        return 0;
    }
    ptr(h)->error.clear();
    for (const auto& r : ptr(h)->roots) if (!cb(u, r.c_str())) break;
    return 1;
}
int such_runtime_list_extensions_v1(such_runtime_handle_v1, such_runtime_string_callback_v1 cb, void* u) { if (!cb) return 0; cb(u,"pdf"); cb(u,"dwg"); return 1; }
void such_runtime_reindex_async_v1(such_runtime_handle_v1 h) { if (h) ++ptr(h)->generation; }
void such_runtime_wait_for_idle_v1(such_runtime_handle_v1) {}
int such_runtime_search_v1(such_runtime_handle_v1 h, const char*, uint32_t, size_t, such_runtime_result_callback_v1 cb, void* u) {
  if (!h || !cb) return 0;
  if (env_flag_enabled("SUCH_FAKE_RUNTIME_FAIL_SEARCH")) {
      ptr(h)->error = "forced fake runtime search failure";
      return 0;
  }
  ptr(h)->error.clear();
  const such_runtime_result_v1 r{7,"project_report.pdf","/stub/project_report.pdf","pdf",1,1,1,1700000000,1234};
  return cb(u,&r) ? 1 : 1;
}
int such_runtime_set_pinned_v1(such_runtime_handle_v1 h, const char*, int) { return h ? 1 : 0; }
int such_runtime_set_indexed_v1(such_runtime_handle_v1 h, const char*, int) { return h ? 1 : 0; }
int such_runtime_status_v1_fn(such_runtime_handle_v1 h, such_runtime_status_v1* s) { if (!h || !s) return 0; s->indexing=0; s->indexed_files=1; s->generation=ptr(h)->generation; return 1; }
int such_runtime_engine_stats_v1_fn(such_runtime_handle_v1 h, such_runtime_engine_stats_v1* s) { if (!h || !s) return 0; *s={}; s->rows=1; s->workers_used=1; s->backend_loaded=1; s->backend_active=1; return 1; }
uint64_t such_runtime_generation_v1(such_runtime_handle_v1 h) { return h ? ptr(h)->generation : 0; }
}
