#pragma once
#include "arrow_abi.h"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace orchiddb {
using Json = nlohmann::json;
struct CompiledQuery { std::string dialect, sql; std::vector<std::string> fields; };

/** Database-free compiler. Serialized query/schema metadata is the only FFI data. */
class Compiler {
  struct Library {
#ifdef _WIN32
    HMODULE handle;
    explicit Library(const char* path) : handle(LoadLibraryA(path)) { if (!handle) throw std::runtime_error("Cannot load OrchidDB compiler"); }
    ~Library() { FreeLibrary(handle); }
    void* symbol(const char* name) { auto p = reinterpret_cast<void*>(GetProcAddress(handle, name)); if (!p) throw std::runtime_error(name); return p; }
#else
    void* handle;
    explicit Library(const char* path) : handle(dlopen(path, RTLD_NOW | RTLD_LOCAL)) { if (!handle) throw std::runtime_error(dlerror()); }
    ~Library() { dlclose(handle); }
    void* symbol(const char* name) { auto p = dlsym(handle, name); if (!p) throw std::runtime_error(name); return p; }
#endif
  };
  std::shared_ptr<Library> library_;
  char* (*compile_)(const char*);
  void (*free_)(char*);
  const char* (*revision_)();
public:
  static std::string default_path() {
    if (auto value = std::getenv("ORCHIDDB_NATIVE_LIBRARY")) return value;
#ifdef _WIN32
    return "orchiddb_compiler.dll";
#elif defined(__APPLE__)
    return "liborchiddb_compiler.dylib";
#else
    return "liborchiddb_compiler.so";
#endif
  }
  explicit Compiler(const std::string& path = default_path()) : library_(std::make_shared<Library>(path.c_str())) {
    auto abi = reinterpret_cast<uint32_t(*)()>(library_->symbol("orchiddb_abi_version"));
    if (abi() != 1) throw std::runtime_error("Unsupported OrchidDB ABI");
    compile_ = reinterpret_cast<char*(*)(const char*)>(library_->symbol("orchiddb_compile_json"));
    free_ = reinterpret_cast<void(*)(char*)>(library_->symbol("orchiddb_string_free"));
    revision_ = reinterpret_cast<const char*(*)()>(library_->symbol("orchiddb_core_revision"));
  }
  std::string core_revision() const { return revision_(); }
  CompiledQuery compile(const Json& request) const {
    const auto encoded = request.dump();
    std::unique_ptr<char, void(*)(char*)> response(compile_(encoded.c_str()), free_);
    if (!response) throw std::runtime_error("Null compiler response");
    auto envelope = Json::parse(response.get());
    if (!envelope.value("ok", false)) throw std::runtime_error(envelope.value("error", "Compiler error"));
    const auto& plan = envelope.at("result");
    if (plan.at("version") != 1 || plan.at("dialect") != request.at("dialect")) throw std::runtime_error("Invalid compiler response");
    return {plan.at("dialect").get<std::string>(), plan.at("sql").get<std::string>(), plan.at("fields").get<std::vector<std::string>>()};
  }
};

/** C Data objects own their release callback independently of the stream. */
template<class T> class ArrowOwner {
  T value_{};
public:
  ArrowOwner() = default;
  explicit ArrowOwner(T value) : value_(value) {}
  ArrowOwner(const ArrowOwner&) = delete;
  ArrowOwner& operator=(const ArrowOwner&) = delete;
  ArrowOwner(ArrowOwner&& other) noexcept : value_(other.value_) { other.value_ = {}; }
  ArrowOwner& operator=(ArrowOwner&& other) noexcept { if (this != &other) { close(); value_ = other.value_; other.value_ = {}; } return *this; }
  ~ArrowOwner() { close(); }
  void close() noexcept { if (value_.release) value_.release(&value_); value_ = {}; }
  T* get() { return &value_; }
  const T* get() const { return &value_; }
  explicit operator bool() const { return value_.release != nullptr; }
};
using RecordBatch = ArrowOwner<ArrowArray>;
using Schema = ArrowOwner<ArrowSchema>;

/** Consumes an ArrowArrayStream: callbacks and ownership move into this object. */
class ArrowResult {
  ArrowOwner<ArrowArrayStream> stream_;
  void check(int code) {
    if (code) { const char* message = stream_.get()->get_last_error(stream_.get()); throw std::runtime_error(message ? message : "Arrow stream failed"); }
  }
  void require_open() { if (!stream_) throw std::runtime_error("Arrow result is closed"); }
public:
  explicit ArrowResult(ArrowArrayStream& stream) : stream_(stream) {
    stream = {};
    if (!stream_ || !stream_.get()->get_schema || !stream_.get()->get_next || !stream_.get()->get_last_error)
      throw std::invalid_argument("Invalid Arrow C stream");
  }
  ArrowResult(ArrowResult&&) = default;
  ArrowResult& operator=(ArrowResult&&) = default;
  Schema schema() { require_open(); Schema out; check(stream_.get()->get_schema(stream_.get(), out.get())); return out; }
  /** A batch with no release callback marks EOF. Prior batches retain independent ownership. */
  RecordBatch next() { require_open(); RecordBatch out; check(stream_.get()->get_next(stream_.get(), out.get())); return out; }
  void close() noexcept { stream_.close(); }
};

/** Engine is borrowed; the returned stream owns only its result resources. */
class ExecutionEngine {
public:
  virtual ~ExecutionEngine() = default;
  virtual std::string id() const = 0;
  virtual std::string dialect() const = 0;
  virtual ArrowResult execute(const CompiledQuery&) = 0;
};
inline ArrowResult query(const Compiler& compiler, const Json& request, ExecutionEngine& engine) {
  if (request.at("dialect") != engine.dialect()) throw std::invalid_argument("Compiler and engine SQL dialects differ");
  return engine.execute(compiler.compile(request));
}
} // namespace orchiddb
