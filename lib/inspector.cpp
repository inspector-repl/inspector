#include "inspector/repl.h"
#include "inspector/PracticalSocket.h"

#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <sstream>

#include <cling/Interpreter/Interpreter.h>
#include <cling/Interpreter/Value.h>
#include <cling/MetaProcessor/MetaProcessor.h>
#include <cling/Utils/Output.h>
#include <llvm/Support/raw_ostream.h>
#include <json/json.h>

#define RCVBUFSIZE 10240

#define RCVBUFSIZE 10240

std::string getJsonInspectorLocation(std::string filePath, unsigned lineNumber){
  Json::Value message;

  message["file"] = filePath.c_str();
  message["line"] = lineNumber;

  Json::StreamWriterBuilder wbuilder;
  return Json::writeString(wbuilder, message);
}

std::string getJsonValue(std::string value){
  Json::Value message;

  message["value"] = value.c_str();

  Json::StreamWriterBuilder wbuilder;
  return Json::writeString(wbuilder, message);
}

void sendInspectorLocationToPrompt(std::string path, unsigned lineNumber, TCPSocket& prompt){
  std::string location = getJsonInspectorLocation(path, lineNumber);
  prompt.send(location.c_str(), location.length() + 1);
}

void sendResultToPrompt(std::string value, TCPSocket& prompt){
  std::string v = getJsonValue(value);
  prompt.send(v.c_str(), v.length() + 1);
}

std::string getCppStatementFromPrompt(TCPSocket& prompt){
  char receiveBuffer[RCVBUFSIZE];
  int bytesReceived = 0;
  int totalBytesReceived = 0;

  do {
    if ((bytesReceived = (prompt.recv(receiveBuffer + totalBytesReceived, RCVBUFSIZE - totalBytesReceived))) <= 0) {
      cerr << "Unable to read";
      exit(1);
    }
    totalBytesReceived += bytesReceived;
  } while (totalBytesReceived < RCVBUFSIZE && receiveBuffer[totalBytesReceived - 1] != '\0');

  receiveBuffer[totalBytesReceived - 1] = '\0';

  std::stringstream ss(receiveBuffer);
  Json::Value jsonMessage;
  ss >> jsonMessage;
  std::string inputValue = jsonMessage["input"].asString();
  return inputValue;
}

extern "C" {

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

    try {
      TCPSocket prompt("localhost", 5000);
      sendInspectorLocationToPrompt(path, lineNumber, prompt);
      while (true) {
        std::string statement = getCppStatementFromPrompt(prompt);
        if (statement == ".quit") {
          sendResultToPrompt("", prompt);
          break;
        }

        if (metaProcessor.process(statement, result, &value, /*disableValuePrinting*/ true)) {
          continue;
        }

        if (result != cling::Interpreter::kSuccess) {
          sendResultToPrompt("Statement is not correct.", prompt);
          continue;
        }
        if (value.isValid()) {
          std::string out;
          {
            llvm::raw_string_ostream os(out);
            value.print(os);
          }
          sendResultToPrompt(out, prompt);
        } else {
          sendResultToPrompt("No result to display.", prompt);
        }

      }

    } catch(SocketException &e) {
      cerr << e.what() << endl;
      exit(1);
    }

  }

  // C++ mangled version of inspectorRunRepl
  void _Z16inspectorRunReplPKcjS0_S0_z(const char* path, unsigned lineNumber, const char* clingContext, ...) __attribute__((weak, alias ("inspectorRunRepl")));
}
