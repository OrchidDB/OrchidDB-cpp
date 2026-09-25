#include "../examples/duckdb_engine.hpp"
#include <iostream>
#define REQUIRE(x) do { if(!(x)) throw std::runtime_error("failed: " #x); } while(false)
void sql(duckdb_connection c,const char* q) {
  duckdb_result result{};
  const auto status=duckdb_query(c,q,&result);
  std::string error=status==DuckDBSuccess?"":duckdb_result_error(&result);
  duckdb_destroy_result(&result);
  if(status!=DuckDBSuccess) throw std::runtime_error(error);
}
int main() {
  duckdb_database db{}; duckdb_connection connection{};
  REQUIRE(duckdb_open(nullptr,&db)==DuckDBSuccess);
  REQUIRE(duckdb_connect(db,&connection)==DuckDBSuccess);
  try {
    sql(connection,"CREATE TABLE people(id BIGINT, name VARCHAR); INSERT INTO people VALUES (9007199254740993,'Orchid'),(2,NULL); BEGIN; INSERT INTO people VALUES (3,'transaction')");
    auto request=orchiddb::Json::parse(R"({"version":1,"dialect":"duckdb","language":"cypher","query":"MATCH (p:Person) RETURN p.id AS id, p.name AS name ORDER BY p.id","tables":[{"name":"people","columns":[{"name":"id","data_type":"int64"},{"name":"name","data_type":"string"}]}],"nodes":[{"label":"Person","table":"people","id":"id","properties":{"id":"id","name":"name"}}]})");
    orchiddb::Compiler compiler; DuckDBEngine engine(connection);
    auto result=orchiddb::query(compiler,request,engine);
    auto schema=result.schema(); REQUIRE(schema.get()->n_children==2);
    REQUIRE(std::string(schema.get()->children[0]->format)=="l");
    auto batch=result.next(); REQUIRE(batch.get()->length==3);
    const auto* ids=static_cast<const int64_t*>(batch.get()->children[0]->buffers[1]);
    REQUIRE(ids[0]==2 && ids[1]==3 && ids[2]==9007199254740993LL);
    REQUIRE(batch.get()->children[1]->null_count==1);
    REQUIRE(!result.next());
    result.close(); result.close();
    // C Data ownership survives closing the result stream.
    REQUIRE(ids[2]==9007199254740993LL);
    sql(connection,"ROLLBACK");
    request["query"]="MATCH (p:Person) RETURN count(p) AS n";
    auto count=orchiddb::query(compiler,request,engine);
    auto count_batch=count.next();
    REQUIRE(static_cast<const int64_t*>(count_batch.get()->children[0]->buffers[1])[0]==2);
    count.close();
    request["query"]="invalid graph query";
    bool failed=false;try {compiler.compile(request);}catch(const std::exception&){failed=true;} REQUIRE(failed);
    request["dialect"]="postgres";
    failed=false;try {orchiddb::query(compiler,request,engine);}catch(const std::invalid_argument&){failed=true;} REQUIRE(failed);
    sql(connection,"SELECT 42");
    std::cout<<"Native compiler, DuckDB "<<duckdb_library_version()<<", Arrow buffers, nulls, int64, ownership, rollback and errors passed\n";
  } catch (...) {duckdb_disconnect(&connection);duckdb_close(&db);throw;}
  duckdb_disconnect(&connection);duckdb_close(&db);
}
