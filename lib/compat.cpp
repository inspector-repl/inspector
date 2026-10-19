#include "compat.h"

#include <clang/Basic/DiagnosticOptions.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/ExecutionEngine/JITLink/JITLinkMemoryManager.h>
#include <llvm/ExecutionEngine/Orc/Core.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h>
#include <llvm/Support/Error.h>
#if LLVM_VERSION_MAJOR < 23
#include <llvm/ExecutionEngine/Orc/SelfExecutorProcessControl.h>
#endif

#include <atomic>
#include <memory>
#include <utility>

#ifdef __linux__
#include <cxxabi.h>
#elif defined(__APPLE__)
// dyld's, no public header declares it.
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
extern "C" void _tlv_atexit(void (*dtor)(void *), void *obj);
#endif

namespace inspector::compat {
namespace {

auto registered() -> std::atomic<bool> & {
  static std::atomic<bool> flag{false};
  return flag;
}

#ifdef __linux__
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): ABI signature
extern "C" auto inspectorThreadAtexit(void (*dtor)(void *), void *obj,
                                      void *dso) noexcept -> int {
  registered() = true;
  return abi::__cxa_thread_atexit(dtor, obj, dso);
}
#elif defined(__APPLE__)
extern "C" void inspectorThreadAtexit(void (*dtor)(void *), void *obj) {
  registered() = true;
  _tlv_atexit(dtor, obj);
}
#endif

} // namespace

auto threadAtexitName() -> const char * {
#ifdef __linux__
  return "__cxa_thread_atexit";
#elif defined(__APPLE__)
  return "_tlv_atexit";
#else
  return nullptr;
#endif
}

auto threadAtexit() -> llvm::orc::ExecutorAddr {
#if defined(__linux__) || defined(__APPLE__)
  return llvm::orc::ExecutorAddr::fromPtr(&inspectorThreadAtexit);
#else
  return {};
#endif
}

auto threadLocalsRegistered() -> bool { return registered(); }

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
