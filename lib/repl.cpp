#include "inspector/repl.h"
#include "inspector/socket.h"
#include "inspector/prompt.h"

#include <clang/Interpreter/Interpreter.h>
#include <clang/Interpreter/Value.h>
#include <clang/Frontend/CompilerInstance.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h>
#include <llvm/ExecutionEngine/Orc/Debugging/DebuggerSupport.h>
#include <llvm/TargetParser/Host.h>

#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <memory>

using namespace inspector;

// Helper function that does the actual work
static void inspectorRunReplImpl(const char* path, unsigned lineNumber, const char* clingDeclare, const char* clingContextFormatted, const char* const* clingIncludes) {
    try {
      // Open socket connection first so we can report any errors
      TCPSocket socket("localhost", 5000);
      Prompt prompt(socket);
      prompt.sendInspectorLocation(path, lineNumber);
      
      llvm::InitializeAllTargetInfos();
      llvm::InitializeAllTargets();
      llvm::InitializeAllTargetMCs();
      llvm::InitializeAllAsmPrinters();
      llvm::InitializeAllAsmParsers();
      
      clang::IncrementalCompilerBuilder CB;
      
      // Set the target triple to match the host
      CB.SetTargetTriple(llvm::sys::getProcessTriple());
      
      // Set up compiler arguments
      std::vector<const char*> Args;
      
      // Add include paths from clingIncludes array
      if (clingIncludes) {
        for (const char* const* includePtr = clingIncludes; *includePtr != nullptr; ++includePtr) {
          Args.push_back(*includePtr);
          std::cerr << "[DEBUG] Adding arg: " << *includePtr << "\n";
        }
      }
      
      std::cerr << "[DEBUG] Total args: " << Args.size() << "\n";
      CB.SetCompilerArgs(Args);
      
      // Create the CompilerInstance
      auto CIOrErr = CB.CreateCpp();
      if (!CIOrErr) {
        std::string errorMsg = "Failed to create CompilerInstance: " + llvm::toString(CIOrErr.takeError());
        prompt.sendResult(errorMsg);
        return;
      }
      
      // Load any required plugins (like clang-repl does)
      (*CIOrErr)->LoadRequestedPlugins();
      
      // Debug: Check if we have proper action support
      std::cerr << "[DEBUG] CompilerInstance created, about to create Interpreter\n";
      
      // Install error handler
      llvm::install_fatal_error_handler([](void *user_data, const char *reason, bool gen_crash_diag) {
        std::cerr << "LLVM fatal error: " << reason << std::endl;
        exit(1);
      }, nullptr);
      
      // Create the interpreter exactly like clang-repl does - no JITBuilder
      auto interpResult = clang::Interpreter::create(std::move(*CIOrErr));
      if (!interpResult) {
        std::string errorMsg = "Failed to create interpreter: " + llvm::toString(interpResult.takeError());
        prompt.sendResult(errorMsg);
        return;
      }
      clang::Interpreter &interp = **interpResult;
      
      // Execute the declarations
      if (auto err = interp.ParseAndExecute(clingDeclare)) {
        std::string errorMsg = "Failed to declare: " + llvm::toString(std::move(err));
        prompt.sendResult(errorMsg);
        // Continue anyway, as this might not be fatal
      }
      
      // Execute the context
      clang::Value value;
      if (auto err = interp.ParseAndExecute(clingContextFormatted, &value)) {
        std::string errorMsg = "Failed to process context: " + llvm::toString(std::move(err));
        prompt.sendResult(errorMsg);
        // Continue anyway, as this might not be fatal
      }
      
      // Main REPL loop
      while (true) {
        std::string statement = prompt.getCppStatement();
        if (statement == ".quit") {
          prompt.sendResult("");
          return;
        }

        // Execute the statement
        clang::Value newValue;
        if (auto err = interp.ParseAndExecute(statement, &newValue)) {
          prompt.sendResult("Statement is not correct: " + llvm::toString(std::move(err)));
          continue;
        }
        value = std::move(newValue);
        if (value.hasValue()) {
          std::string out;
          {
            llvm::raw_string_ostream os(out);
            value.print(os);
          }
          prompt.sendResult(out);
        } else {
          prompt.sendResult("No result to display.");
        }

      }

    } catch(SocketException &e) {
      cerr << e.what() << endl;
      exit(1);
    }

}

extern "C" {
  void inspectorRunRepl(const char* path, unsigned lineNumber, const char* clingDeclare, const char* clingContext, const char* const* clingIncludes, ...) {
    va_list arglist;

    // Format the context string with variadic arguments
    va_start(arglist, clingIncludes);
    int bufferSize = vsnprintf(0, 0, clingContext, arglist);
    va_end(arglist);

    auto clingContextBuffer = std::make_unique<char[]>(bufferSize + 1);
    va_start(arglist, clingIncludes);
    vsnprintf(clingContextBuffer.get(), bufferSize + 1, clingContext, arglist);
    va_end(arglist);
    
    // Call the implementation
    inspectorRunReplImpl(path, lineNumber, clingDeclare, clingContextBuffer.get(), clingIncludes);
  }

  // C++ mangled version of inspectorRunRepl
  // Define the function separately to avoid platform-specific alias issues
  __attribute__((weak))
  void _Z16inspectorRunReplPKcjS0_S0_PKS0_z(const char* path, unsigned lineNumber, const char* clingDeclare, const char* clingContext, const char* const* clingIncludes, ...) {
    va_list arglist;
    
    // Format the context string with variadic arguments
    va_start(arglist, clingIncludes);
    int bufferSize = vsnprintf(0, 0, clingContext, arglist);
    va_end(arglist);
    
    auto contextBuffer = std::make_unique<char[]>(bufferSize + 1);
    va_start(arglist, clingIncludes);
    vsnprintf(contextBuffer.get(), bufferSize + 1, clingContext, arglist);
    va_end(arglist);
    
    // Call the implementation
    inspectorRunReplImpl(path, lineNumber, clingDeclare, contextBuffer.get(), clingIncludes);
  }
}
