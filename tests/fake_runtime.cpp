#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <such/runtime/RuntimeABI.h>
#include <string>
#include <vector>

namespace {
struct Stub { std::string error; std::vector<std::string> roots; std::uint64_t generation = 1; };
Stub* ptr(such_runtime_handle_v1 h) { return static_cast<Stub*>(h); }

std::string env_value(const char* name) {
#if defined(_WIN32)
    char* buffer = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&buffer, &length, name) != 0 || buffer == nullptr) return {};
    std::string value(buffer);
    std::free(buffer);
    return value;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string{};
#endif
}

bool env_flag_enabled(const char* name) {
    return !env_value(name).empty();
}
}
extern "C" {
uint32_t such_runtime_abi_version(void) { return SUCH_RUNTIME_ABI_VERSION; }
such_runtime_handle_v1 such_runtime_create_v1(const char*) {
    auto* stub = new Stub{};
    const std::string initial_root = env_value("SUCH_FAKE_RUNTIME_INITIAL_ROOT");
    if (!initial_root.empty()) stub->roots.emplace_back(initial_root);
    return stub;
}
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
int such_runtime_search_v1(such_runtime_handle_v1 h, const char* query_utf8, uint32_t, size_t, such_runtime_result_callback_v1 cb, void* u) {
  const std::string query_log = env_value("SUCH_FAKE_RUNTIME_QUERY_LOG");
  if (!query_log.empty() && query_utf8 != nullptr) {
      FILE* file = nullptr;
#if defined(_WIN32)
      if (fopen_s(&file, query_log.c_str(), "ab") == 0 && file != nullptr) {
#else
      file = ::fopen(query_log.c_str(), "ab");
      if (file != nullptr) {
#endif
          std::fprintf(file, "%s\n", query_utf8);
          std::fclose(file);
      }
  }
  if (!h || !cb) return 0;
  if (env_flag_enabled("SUCH_FAKE_RUNTIME_FAIL_SEARCH")) {
      ptr(h)->error = "forced fake runtime search failure";
      return 0;
  }
  ptr(h)->error.clear();
  if (query_utf8 != nullptr && std::string(query_utf8) == "mixedsuffix") {
      const such_runtime_result_v1 rejected{50,"internal.oddprivext","/stub/internal.oddprivext","oddprivext",0,1,1,1700000000,23};
      const such_runtime_result_v1 accepted{51,"main.cpp","/stub/main.cpp","cpp",0,1,1,1700000000,34};
      if (!cb(u,&rejected)) return 1;
      (void)cb(u,&accepted);
      return 1;
  }
  const std::string korean_query = "\xED\x95\x9C\xEA\xB8\x80 \xEA\xB2\x80\xEC\x83\x89";
  const bool exact_korean_utf8 = query_utf8 != nullptr && std::string(query_utf8) == korean_query;
  const such_runtime_result_v1 regular{7,"project_report.pdf","/stub/project_report.pdf","pdf",1,1,1,1700000000,1234};
  const such_runtime_result_v1 unicode{8,"unicode_hit.pdf","/stub/unicode_hit.pdf","pdf",0,1,1,1700000000,1234};
  if (query_utf8 != nullptr && std::string(query_utf8) == "project") {
      const such_runtime_result_v1 text{9,"project_report_notes.txt","/stub/project_report_notes.txt","txt",0,1,1,1700000000,234};
      if (!cb(u, &regular)) return 1;
      (void)cb(u, &text);
      return 1;
  }
  return cb(u, exact_korean_utf8 ? &unicode : &regular) ? 1 : 1;
}
int such_runtime_search_content_v1(
    such_runtime_handle_v1 h,
    const such_runtime_content_search_request_v1* req,
    such_runtime_content_result_callback_v1 cb,
    void* u) {
    if (!h || !req || !req->query_utf8 || !cb) return 0;
    ptr(h)->error.clear();
    bool candidate_ok = req->candidate_file_count == 0;
    for (size_t i = 0; i < req->candidate_file_count; ++i) {
        if (req->candidate_file_ids != nullptr && req->candidate_file_ids[i] == 9) candidate_ok = true;
    }
    if (!candidate_ok || *req->query_utf8 == '\0') return 1;
    const std::string query(req->query_utf8);
    if (query.find("warranty") == std::string::npos && query.find("renderer") == std::string::npos) return 1;
    const such_runtime_content_result_v1 rejected{
        7, "project_report.docx", "/stub/project_report.docx", "docx",
        SUCH_CONTENT_LOCATOR_LINE, 0, 41, 0, 0, 64, "",
        "binary document should not be a content result", 1.0};
    const such_runtime_content_result_v1 accepted{
        9, "project_report_notes.txt", "/stub/project_report_notes.txt", "txt",
        SUCH_CONTENT_LOCATOR_LINE, 0, 42, 0, 0, 128, "",
        "warranty renderer content", 1.0};
    if (!cb(u, &rejected)) return 1;
    (void)cb(u, &accepted);
    return 1;
}
int such_runtime_set_pinned_v1(such_runtime_handle_v1 h, const char*, int) { return h ? 1 : 0; }
int such_runtime_set_indexed_v1(such_runtime_handle_v1 h, const char*, int) { return h ? 1 : 0; }
int such_runtime_status_v1_fn(such_runtime_handle_v1 h, such_runtime_status_v1* s) { if (!h || !s) return 0; s->indexing=0; s->indexed_files=1; s->generation=ptr(h)->generation; return 1; }
int such_runtime_engine_stats_v1_fn(such_runtime_handle_v1 h, such_runtime_engine_stats_v1* s) { if (!h || !s) return 0; *s={}; s->rows=1; s->workers_used=1; s->backend_loaded=1; s->backend_active=1; return 1; }
uint64_t such_runtime_generation_v1(such_runtime_handle_v1 h) { return h ? ptr(h)->generation : 0; }
}
