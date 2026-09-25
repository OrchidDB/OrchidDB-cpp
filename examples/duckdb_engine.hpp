#pragma once
#include <orchiddb/orchiddb.hpp>
#include <duckdb.h>
#include <cerrno>

/** Example borrowed-connection adapter. DuckDB dependency belongs to application. */
class DuckDBEngine final : public orchiddb::ExecutionEngine {
  duckdb_connection connection_;
  struct State {
    duckdb_arrow result{};
    ~State() { if(result) duckdb_destroy_arrow(&result); }
  };
public:
  explicit DuckDBEngine(duckdb_connection connection) : connection_(connection) {}
  std::string id() const override { return "caller-duckdb"; }
  std::string dialect() const override { return "duckdb"; }
  orchiddb::ArrowResult execute(const orchiddb::CompiledQuery& plan) override {
    if(plan.dialect != dialect()) throw std::invalid_argument("Wrong SQL dialect");
    auto state=std::make_unique<State>();
    if(duckdb_query_arrow(connection_,plan.sql.c_str(),&state->result)!=DuckDBSuccess)
      throw std::runtime_error(duckdb_query_arrow_error(state->result));
    ArrowArrayStream stream{};
    stream.private_data=state.release();
    stream.get_schema=[](ArrowArrayStream* s,ArrowSchema* out) {
      auto ptr=reinterpret_cast<duckdb_arrow_schema>(out);
      return duckdb_query_arrow_schema(static_cast<State*>(s->private_data)->result,&ptr)==DuckDBSuccess?0:EIO;
    };
    stream.get_next=[](ArrowArrayStream* s,ArrowArray* out) {
      auto ptr=reinterpret_cast<duckdb_arrow_array>(out);
      return duckdb_query_arrow_array(static_cast<State*>(s->private_data)->result,&ptr)==DuckDBSuccess?0:EIO;
    };
    stream.get_last_error=[](ArrowArrayStream* s) {return duckdb_query_arrow_error(static_cast<State*>(s->private_data)->result);};
    stream.release=[](ArrowArrayStream* s) {delete static_cast<State*>(s->private_data);s->release=nullptr;s->private_data=nullptr;};
    return orchiddb::ArrowResult(stream);
  }
};
