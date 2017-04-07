#include <iostream>
#include "Analysis.h"
#include <map>
#include "json.h"

using json = nlohmann::json;

typedef void (*RequestHandler)(const json&);

static bool stop_analysis = false;

std::map<std::string, RequestHandler> request_handlers;

void Quit(const json& request){
  std::cout << json({{"response", "quit"}}) << std::endl;
  stop_analysis = true;
}

void ErrorResponse(const std::string& message){
  std::cout << json({{"response", "error"}, {"message", message}}) << std::endl;
}

void AnalysePosition(const json& request){

}

void AnalyseGame(const json& request) {

}

void Analysis_main(){
  request_handlers = {
      {"quit", Quit},
      {"analyse-position", AnalysePosition},
      {"analyse-game", AnalyseGame}
  };

  while(!stop_analysis){
    json request;
    try {
      std::string line;
      std::getline(std::cin, line);
      request = json::parse(line);
    } catch (...) {
      ErrorResponse("Invalid request.");
      continue;
    }
    std::string request_type = request["request"];
    if (request_handlers.count(request_type)){
      request_handlers[request_type](request);
    } else {
      ErrorResponse("Invalid request type.");
    }
  }
}