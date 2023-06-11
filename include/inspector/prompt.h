#include "inspector/common.h"
#include "inspector/socket.h"

#include <cling/Utils/Output.h>
#include <llvm/Support/raw_ostream.h>
#include <json/json.h>

#define RCVBUFSIZE 10240

namespace inspector {
  class Prompt {
      TCPSocket& socket;
      std::string getJsonInspectorLocation(std::string filePath, unsigned lineNumber);
      std::string getJsonValue(std::string value);

    public:
      Prompt(TCPSocket &socket);
      void sendInspectorLocation(std::string path, unsigned lineNumber);
      void sendResult(std::string value);
      std::string getCppStatement();
  };
}
