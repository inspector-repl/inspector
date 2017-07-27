#include "inspector/prompt.h"

#include <llvm/Support/raw_ostream.h>
#include <json/json.h>

#include <sstream>
#include <iostream>

#define RCVBUFSIZE 10240

using namespace inspector;

Prompt::Prompt(TCPSocket &socket_) : socket(socket_) {};

string Prompt::getJsonInspectorLocation(std::string filePath, unsigned lineNumber) {
  Json::Value message;

  message["file"] = filePath.c_str();
  message["line"] = lineNumber;

  Json::StreamWriterBuilder wbuilder;
  return Json::writeString(wbuilder, message);
}

std::string Prompt::getJsonValue(std::string value) {
  Json::Value message;

  message["value"] = value.c_str();

  Json::StreamWriterBuilder wbuilder;
  return Json::writeString(wbuilder, message);
}

void Prompt::sendInspectorLocation(std::string path, unsigned lineNumber) {
  std::string location = getJsonInspectorLocation(path, lineNumber);
  socket.send(location.c_str(), location.length() + 1);
}

void Prompt::sendResult(std::string value) {
  std::string v = getJsonValue(value);
  socket.send(v.c_str(), v.length() + 1);
}

std::string Prompt::getCppStatement() {
  char receiveBuffer[RCVBUFSIZE];
  int bytesReceived = 0;
  int totalBytesReceived = 0;

  do {
    if ((bytesReceived = (socket.recv(receiveBuffer + totalBytesReceived, RCVBUFSIZE - totalBytesReceived))) <= 0) {
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
