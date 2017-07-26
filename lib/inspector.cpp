#include "inspector/repl.h"

#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <sstream>

#include <cling/Interpreter/Interpreter.h>
#include <cling/Interpreter/Value.h>
#include <cling/MetaProcessor/MetaProcessor.h>
#include <cling/Utils/Output.h>
#include <llvm/Support/raw_ostream.h>
#include <jsoncpp/json/json.h>
#include <cpprest/http_client.h>
#include <cpprest/json.h>

extern "C" {
  void printJsonMessage(Json::Value message){
    std::cout << message << std::endl;
  }

  std::string sendRequest(std::string message){
    // Http client.
    web::http::client::http_client client(U("http://localhost:5000"));

    // URI builder.
    web::uri_builder builder(U("/"));

    web::json::value body = web::json::value::parse(message);
    pplx::task<web::http::http_response> resp = client.request(web::http::methods::POST, builder.to_string(), body);
    //return resp.get().to_string();
    resp.get();
    return "";
  }

  std::string getJsonRequest(
        unsigned id,
        std::string method,
        std::string path,
        unsigned lineNumber,
        std::string clingContext) {
    Json::Value request;
    request["id"] = id;
    request["jsonrpc"] = "2.0";
    request["method"] = method.c_str();
    request["params"]["path"] = path.c_str();
    request["params"]["lineNumber"] = lineNumber;
    request["params"]["clingContext"] = clingContext.c_str();

    Json::StreamWriterBuilder wbuilder;
    return Json::writeString(wbuilder, request);
  }

  void inspectorRunRepl(const char* path, unsigned lineNumber, const char* clingDeclare, const char* clingContext, ...) {
    va_list arglist;

    // pass all pointer to cling
    va_start(arglist, clingContext);
    int bufferSize = vsnprintf(0, 0, clingContext, arglist);
    va_end(arglist);

    char* clingContextBuffer = new char[bufferSize + 1];
    va_start(arglist, clingContext);
    vsnprintf(clingContextBuffer, bufferSize + 1, clingContext, arglist);
    va_end(arglist);

    const char* argv = "cling";
    cling::Interpreter interp(1, &argv, LLVMDIR);
    interp.declare(clingDeclare);

    cling::MetaProcessor metaProcessor(interp, cling::errs());

    cling::Value value;
    cling::Interpreter::CompilationResult result;
    metaProcessor.process(clingContextBuffer, result, &value, /*disableValuePrinting*/ true);
    delete []clingContextBuffer;

    while (true) {
      std::string request = getJsonRequest(1, "prompt", path, lineNumber, clingContext);
      std::string in = sendRequest(request);
      std::cout << "processing: " << in << std::endl;
      if (metaProcessor.process(in, result, &value, /*disableValuePrinting*/ true)) {
        continue;
      }
      if (result != cling::Interpreter::kSuccess) {
        continue;
      }
      if (value.isValid()) {
        std::string out;
        {
          llvm::raw_string_ostream os(out);
          value.print(os);
        }
        std::cout << out << std::endl;
      }
    }
  }

  // C++ mangled version of inspectorRunRepl
  void _Z16inspectorRunReplPKcjS0_S0_z(const char* path, unsigned lineNumber, const char* clingContext, ...) __attribute__((weak, alias ("inspectorRunRepl")));
}
