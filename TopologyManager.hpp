#ifndef TOPOLOGY_MANAGER_HPP
#define TOPOLOGY_MANAGER_HPP

#include <atomic>
#include <thread>
#include <string>
#include <mutex>
#include <array>
#include <boost/graph/adjacency_list.hpp>

class TopologyManager {
public:
  enum class VertexType { SWITCH, HOST };
  struct VertexProperties {
    VertexType vertex_type;
    std::string switch_dpid;
    std::string host_ip_addr;
  };
  using Graph = boost::adjacency_list<boost::vecS, boost::vecS, boost::undirectedS, VertexProperties>;

  TopologyManager(const std::array<std::string, 3>& ryuUrl);
  ~TopologyManager();

  void start();
  void stop();

  Graph getGraph();

  void printGraph();

private:
  void run();

  void fetchAndUpdateTopologyData();

  void updateSwitches(const std::string& topologyData);

  void updateHosts(const std::string& topologyData);

  void updateLinks(const std::string& topologyData);

  void updateGraph(const std::string&, const std::string&, const std::string&);

  std::array<std::string, 3> m_ryuUrl;

  Graph m_graph;

  Graph::vertex_descriptor findSwitchByDpid(const std::string& dpid);

  std::mutex m_graphMutex;

  std::atomic<bool> m_running{ true };

  std::thread m_thread;
};

#endif // TOPOLOGY_MANAGER_HPP
