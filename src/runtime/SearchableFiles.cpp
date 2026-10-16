#include <such/runtime/SearchableFiles.h>

#include <algorithm>
#include <array>
#include <string_view>

namespace such::runtime {
namespace {
char lower_ascii(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
}
bool equals_ascii_ci(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (lower_ascii(a[i]) != lower_ascii(b[i])) return false;
    return true;
}
// Ordinary documents, media, archives, CAD assets, installers, and development
// artifacts. An unknown suffix is not automatically trusted just because an
// older runtime observed it on disk.
constexpr std::array<std::string_view, 315> kAllowedExtensions{
    "3dm", "3ds", "7z", "a", "aac", "adoc", "ai", "apk", "arrow", "asm",
    "ass", "astro", "avi", "avif", "awk", "azw", "azw3", "bash", "bat", "bib",
    "bicep", "blend", "bmp", "bz2", "c", "c++", "cab", "cc", "cfg", "cjs",
    "clang-format", "clang-tidy", "class", "clj", "cljc", "cljs", "cmake", "cmd", "comp", "conf",
    "coverage", "cpp", "cr2", "crate", "cs", "css", "csv", "cu", "cuh", "cxx",
    "dae", "dart", "db", "db3", "deb", "def", "diff", "djvu", "dll", "dmg",
    "dng", "doc", "dockerignore", "docm", "docx", "dwg", "dws", "dwt", "dxf", "dylib",
    "ear", "editorconfig", "edn", "egg", "elm", "env", "eot", "eps", "epub", "erl",
    "ex", "exe", "expect", "exs", "fbx", "feather", "fish", "flac", "frag", "fs",
    "fsx", "fx", "gcda", "gcno", "gem", "geom", "gif", "gitattributes", "gitignore", "glb",
    "glsl", "gltf", "go", "gql", "gradle", "gradle.kts", "graphql", "groovy", "gz", "h",
    "h5", "hcl", "hdf5", "heic", "heif", "hh", "hlsl", "hpp", "hrl", "hs",
    "htm", "html", "hwp", "hwpx", "hxx", "icns", "ico", "idl", "ifc", "ifczip",
    "iges", "igs", "img", "ini", "inl", "ipp", "ipynb", "iso", "ixx", "jar",
    "java", "jl", "jpeg", "jpg", "js", "json", "jsonl", "jsx", "key", "kt",
    "kts", "lcov", "less", "lhs", "lib", "lock", "log", "lua", "lz4", "m",
    "m4a", "m4v", "make", "map", "markdown", "md", "mid", "midi", "mjs", "mk",
    "mkv", "mm", "mobi", "mov", "mp3", "mp4", "msi", "msix", "nasm", "nef",
    "nim", "nix", "npmrc", "npy", "npz", "numbers", "o", "obj", "odp", "ods",
    "odt", "ogg", "onnx", "opus", "otf", "pages", "parquet", "patch", "pdb", "pdf",
    "php", "pickle", "pkl", "pl", "pm", "png", "ppt", "pptm", "pptx", "prettierrc",
    "pri", "pro", "properties", "proto", "ps1", "psd", "psd1", "psm1", "pt", "pth",
    "py", "pyi", "pyz", "qmake", "qrc", "qss", "r", "rake", "rar", "raw",
    "rb", "rc", "resx", "rfa", "rft", "rmd", "rpm", "rs", "rst", "rte",
    "rtf", "rvt", "rws", "s", "safetensors", "sass", "sat", "sbt", "sc", "scala",
    "scss", "sed", "sh", "skp", "so", "sql", "sqlite", "sqlite3", "srt", "step",
    "stl", "stp", "sv", "svelte", "svg", "swift", "tar", "tcl", "tex", "text",
    "tf", "tfvars", "tgz", "tif", "tiff", "toml", "ts", "tsv", "tsx", "ttf",
    "txt", "ui", "usd", "usdz", "v", "vb", "vert", "vhd", "vhdl", "vtt",
    "vue", "war", "wasm", "wav", "webm", "webp", "wgsl", "whl", "wma", "wmv",
    "woff", "woff2", "xcf", "xls", "xlsm", "xlsx", "xml", "xsd", "xz", "yaml",
    "yml", "zig", "zip", "zsh", "zst",
};
constexpr std::array<std::string_view, 29> kExtensionlessNames{
    "Makefile", "GNUmakefile", "Dockerfile", "Containerfile", "CMakeLists.txt",
    "Justfile", "Taskfile", "Procfile", "Vagrantfile", "Rakefile",
    "Gemfile", "Guardfile", "Brewfile", "Podfile", "Fastfile", "Appfile",
    "README", "LICENSE", "NOTICE", "COPYING", "AUTHORS", "CHANGELOG",
    "CONTRIBUTING", "CODEOWNERS", "BUILD", "BUILD.bazel", "WORKSPACE",
    "WORKSPACE.bazel", "MODULE.bazel"
};
}
bool searchable_filename(std::string_view filename, std::string_view extension) noexcept {
    if (filename.empty()) return false;
    // Prefer the actual filename: private runtime metadata may be empty or
    // refer to a multipart suffix. Do not accept a spoofed extension field.
    const auto last_sep = filename.find_last_of("/\\");
    if (last_sep != std::string_view::npos) filename.remove_prefix(last_sep + 1);
    if (filename.empty()) return false;
    for (const auto special : kExtensionlessNames) if (equals_ascii_ci(filename, special)) return true;
    const auto dot = filename.find_last_of('.');
    if (dot == std::string_view::npos || dot + 1 == filename.size()) return false;
    if (dot == 0 && filename.find('.', 1) == std::string_view::npos) {
        // Developer dotfiles such as .gitignore are recognized by name.
        extension = filename.substr(1);
    } else {
        extension = filename.substr(dot + 1);
    }
    // Sorted constexpr table: O(log N) lookups, no heap allocations or
    // mutable global cache, even for large file-result sets.
    std::size_t low = 0;
    std::size_t high = kAllowedExtensions.size();
    while (low < high) {
        const auto middle = low + (high - low) / 2;
        const auto candidate = kAllowedExtensions[middle];
        const auto length = std::min(extension.size(), candidate.size());
        int compare = 0;
        for (std::size_t i = 0; i < length; ++i) {
            const char lhs = lower_ascii(extension[i]);
            const char rhs = candidate[i];
            if (lhs != rhs) { compare = lhs < rhs ? -1 : 1; break; }
        }
        if (compare == 0 && extension.size() != candidate.size())
            compare = extension.size() < candidate.size() ? -1 : 1;
        if (compare > 0) low = middle + 1;
        else high = middle;
    }
    return low != kAllowedExtensions.size() && equals_ascii_ci(extension, kAllowedExtensions[low]);
}
} // namespace such::runtime

namespace such::runtime {
bool searchable_text_filename(std::string_view filename, std::string_view extension) noexcept {
    if (!searchable_filename(filename, extension)) return false;
    const auto sep = filename.find_last_of("/\\");
    if (sep != std::string_view::npos) filename.remove_prefix(sep + 1);
    for (const auto name : kExtensionlessNames) if (equals_ascii_ci(filename, name)) return true;
    auto dot = filename.find_last_of('.');
    if (dot == std::string_view::npos) return false;
    extension = filename.substr(dot + 1);
    if (dot == 0 && filename.find('.', 1) == std::string_view::npos) extension = filename.substr(1);
    // Conservative, no-IO presentation filter. The private runtime still must
    // verify UTF-8, binary magic and selected-root scope BEFORE scanning.
    constexpr std::string_view allowed[]{
        "adoc","asm","astro","awk","bash","bat","bib","bicep","c","cc",
        "clang-format","clang-tidy","dockerignore",
        "cfg","cjs","clj","cljc","cljs","cmake","cmd","conf","cpp","cs",
        "css","cu","cuh","cxx","dart","diff","editorconfig","edn","elm","env",
        "erl","ex","exs","fish","frag","fs","fsx","gitattributes","gitignore","glsl",
        "go","gql","gradle","graphql","groovy","h","hcl","hh","hlsl","hpp",
        "hrl","hs","htm","html","idl","ini","inl","ipp","ixx","java",
        "jl","js","json","jsonl","jsx","kt","kts","less","lhs","lock","log",
        "lua","m","markdown","md","mjs","mk","nasm","nim","nix","npmrc",
        "php","pl","pm","prettierrc","properties","proto","ps1","psd1","psm1","py",
        "pyi","r","rb","rmd","rs","rst","s","sass","scala","scss",
        "sed","sh","sql","srt","sv","svelte","tex","text","tf","toml",
        "ts","tsx","txt","xml","yaml","yml","zig","zsh","csv","tsv"
    };
    for (const auto candidate : allowed) if (equals_ascii_ci(extension, candidate)) return true;
    return false;
}
} // namespace such::runtime
