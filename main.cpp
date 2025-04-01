#include "SFlowCollector.hpp"
// #include "TopologyManager.hpp"
#include <array>

int main() {
  sflow::SFlowCollector collector;
  collector.start();

  // std::array<std::string,3> ryuUrl;
  // ryuUrl[0] = "http://localhost:8080/v1.0/topology/switches";
  // ryuUrl[1] = "http://localhost:8080/v1.0/topology/hosts"; 
  // ryuUrl[2] = "http://localhost:8080/v1.0/topology/links";
  
  // TopologyManager topologyManager(ryuUrl);
  // topologyManager.start();
  return 0;
}
