#include "TopologyManager.hpp"
#include <iostream>
#include <stdexcept>
#include <cstdio>
#include <array>
#include <memory>
#include <chrono>
#include <sstream>
#include <nlohmann/json.hpp>

using namespace std;
using json = nlohmann::json;

// Utility function: execute a command and return its output.
string execCommand(const std::string& cmd) {
  array<char, 128> buffer;
  string result;
  // Open a pipe to run the command.
  using PipeCloser = int(*)(FILE*);
  unique_ptr<FILE, PipeCloser> pipe(popen(cmd.c_str(), "r"), pclose);
  if (!pipe) {
    throw runtime_error("popen() failed!");
  }
  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
    result += buffer.data();
  }
  return result;
}

TopologyManager::TopologyManager(const std::array<std::string, 3>& ryuUrl)
  : m_ryuUrl(ryuUrl), m_running(false)
{
  // You can initialize m_graph if needed.
}

TopologyManager::~TopologyManager() {
  stop();
}

void TopologyManager::start() {
  m_running.store(true);
  m_thread = std::thread(&TopologyManager::run, this);
}

void TopologyManager::stop() {
  m_running.store(false);
  if (m_thread.joinable()) {
    m_thread.join();
  }
}

TopologyManager::Graph TopologyManager::getGraph() {
  std::lock_guard<std::mutex> lock(m_graphMutex);
  // Returns a copy of the current graph.
  return m_graph;
}

void TopologyManager::fetchAndUpdateTopologyData() {
  // Get switches
  std::string curlCommand = "curl -s -X GET " + m_ryuUrl[0];
  std::string switchesStr;
  try {
    switchesStr = execCommand(curlCommand);
  }
  catch (const std::exception& ex) {
    std::cerr << "Error executing curl command: " << ex.what() << std::endl;
    return;
  }

  curlCommand = "curl -s -X GET " + m_ryuUrl[1];
  std::string hostsStr;
  try {
    hostsStr = execCommand(curlCommand);
  }
  catch (const std::exception& ex) {
    std::cerr << "Error executing curl command: " << ex.what() << std::endl;
    return;
  }

  curlCommand = "curl -s -X GET " + m_ryuUrl[2];
  std::string linksStr;
  try {
    linksStr = execCommand(curlCommand);
  }
  catch (const std::exception& ex) {
    std::cerr << "Error executing curl command: " << ex.what() << std::endl;
    return;
  }

  updateGraph(switchesStr, hostsStr, linksStr);
}

void TopologyManager::updateSwitches(const std::string& topologyData) {
  try {
    auto j = json::parse(topologyData);

    for (const auto& sw : j) {
      std::string dpid = sw.value("dpid", "");
      // std::cout << dpid << std::endl;
      a
      

      auto vertex = boost::add_vertex(m_graph);
      m_graph[vertex].vertex_type = VertexType::SWITCH;
      m_graph[vertex].switch_dpid = dpid;
    }
  }
  catch (const json::parse_error& err) {
    std::cerr << "JSON parse error: " << err.what() << std::endl;
    return;
  }
}

void TopologyManager::updateHosts(const std::string& topologyData) {
  try {
    auto j = json::parse(topologyData);
    std::cout << j << std::endl;
    for (const auto& host : j) {
      std::string ip = host["ipv4"].empty() ? "" : host["ipv4"][0];
      // TODO: check ip's correctness
      if (ip.empty() or ip == "0.0.0.0") continue;
      // std::cout << ip << std::endl;
      auto vertex = boost::add_vertex(m_graph);
      m_graph[vertex].vertex_type = VertexType::HOST;
      m_graph[vertex].host_ip_addr = ip;
    }
  }
  catch (const json::parse_error& err) {
    std::cerr << "JSON parse error: " << err.what() << std::endl;
    return;
  }
}

void TopologyManager::updateLinks(const std::string& topologyData) {
  try {
    auto j = json::parse(topologyData);

    for (const auto& link : j) {
      std::string src_dpid = link["src"].value("dpid", "");
      std::string dst_dpid = link["dst"].value("dpid", "");

      if (src_dpid.empty() || dst_dpid.empty()) continue;

      // Check if both switches exist in the graph
      try {
        auto src_vertex = findSwitchByDpid(src_dpid);
        auto dst_vertex = findSwitchByDpid(dst_dpid);
        boost::add_edge(src_vertex, dst_vertex, m_graph);
      }
      catch (const std::runtime_error& e) {
        std::cerr << "Warning: " << e.what() << std::endl;
      }
    }
  }
  catch (const json::parse_error& err) {
    std::cerr << "JSON parse error: " << err.what() << std::endl;
    return;
  }
}

void TopologyManager::updateGraph(const std::string& switchesStr, const std::string& hostsStr, const std::string& linksStr) {
  std::lock_guard<std::mutex> lock(m_graphMutex);
  updateSwitches(switchesStr);
  updateHosts(hostsStr);
  updateLinks(linksStr);
  // printGraph();
}

void TopologyManager::printGraph() {
  std::cout << "=== Vertices ===" << std::endl;
  for (auto [vi, vi_end] = boost::vertices(m_graph); vi != vi_end; ++vi) {
    const auto& v = *vi;
    const auto& prop = m_graph[v];
    std::cout << (prop.vertex_type == VertexType::SWITCH ? "[Switch] " : "[Host] ")
      << "dpid: " << prop.switch_dpid << " addr: " << prop.host_ip_addr << "\n";
    std::cout << std::endl;
  }
  std::cout << "\n=== Edges ===" << std::endl;
  for (auto [ei, ei_end] = boost::edges(m_graph); ei != ei_end; ++ei) {
    auto src = boost::source(*ei, m_graph);
    auto tgt = boost::target(*ei, m_graph);
    std::cout << m_graph[src].switch_dpid<< " <--> " << m_graph[tgt].switch_dpid << std::endl;
  }
}

TopologyManager::Graph::vertex_descriptor TopologyManager::findSwitchByDpid(const std::string& dpid) {
  for (auto [vi, vi_end] = boost::vertices(m_graph); vi != vi_end; ++vi) {
    if (m_graph[*vi].vertex_type == VertexType::SWITCH and m_graph[*vi].switch_dpid == dpid) {
      return *vi;
    }
  }
  throw std::runtime_error("Switch not found: " + dpid);
}

void TopologyManager::run() {
  // std::cout << m_running.load() << std::endl;
  while (true) {
    fetchAndUpdateTopologyData();
    // printGraph();
    // Sleep for a while (e.g., 5 seconds) before fetching topology data again.
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}
