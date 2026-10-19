#include "compat.h"

#include <clang/Basic/DiagnosticOptions.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/ExecutionEngine/JITLink/JITLinkMemoryManager.h>
#include <llvm/ExecutionEngine/Orc/Core.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/Support/Error.h>
#if LLVM_VERSION_MAJOR < 23
#include <llvm/ExecutionEngine/Orc/SelfExecutorProcessControl.h>
#endif

#include <memory>
#include <utility>

namespace inspector::compat {

void showColors(clang::DiagnosticOptions &opts) {
#if LLVM_VERSION_MAJOR >= 23
  opts.setShowColors(clang::ShowColorsKind::On);
#else
  opts.ShowColors = true;
#endif
}

auto setMemoryManager(
    llvm::orc::LLJITBuilder &jit,
    std::unique_ptr<llvm::jitlink::JITLinkMemoryManager> memory)
    -> llvm::Error {
#if LLVM_VERSION_MAJOR >= 23
  // std::function needs a copyable callable, so share the one-shot pointer.
  const auto shared =
      std::make_shared<std::unique_ptr<llvm::jitlink::JITLinkMemoryManager>>(
          std::move(memory));
  jit.setMemoryManagerCreator(
      [shared](llvm::orc::ExecutionSession &)
          -> llvm::Expected<
              std::unique_ptr<llvm::jitlink::JITLinkMemoryManager>> {
        return std::move(*shared);
      });
#else
  // LLVM 22 only takes the memory manager through the process control.
  auto process = llvm::orc::SelfExecutorProcessControl::Create(
      nullptr, nullptr, std::move(memory));
  if (!process) {
    return process.takeError();
  }
  jit.setExecutorProcessControl(std::move(*process));
#endif
  return llvm::Error::success();
}

} // namespace inspector::compat
