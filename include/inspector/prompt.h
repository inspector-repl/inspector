#include "inspector/common.h"
#include "inspector/socket.h"

#include <json/json.h>
#include <llvm/Support/raw_ostream.h>

#define RCVBUFSIZE 10240

namespace inspector {
class Prompt {
  UnixSocket &socket;
  std::string getJsonInspectorLocation(std::string filePath,
                                       unsigned lineNumber);
  std::string getJsonValue(std::string value);

public:
  Prompt(UnixSocket &socket);
  void sendInspectorLocation(std::string path, unsigned lineNumber);
  void sendResult(std::string value);
  std::string getCppStatement();
};
} // namespace inspector
