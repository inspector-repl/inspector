// Differences between the supported LLVM versions, kept out of the rest of
// the code.
#pragma once

#include <clang/Basic/DiagnosticOptions.h>
#include <llvm/ExecutionEngine/JITLink/JITLinkMemoryManager.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/Support/Error.h>

#include <memory>

namespace inspector::compat {

void showColors(clang::DiagnosticOptions &opts);

// Must be called before any other ExecutorProcessControl setting.
auto setMemoryManager(
    llvm::orc::LLJITBuilder &jit,
    std::unique_ptr<llvm::jitlink::JITLinkMemoryManager> memory) -> llvm::Error;

} // namespace inspector::compat
