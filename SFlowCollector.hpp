#ifndef SFLOW_COLLECTOR_HPP
#define SFLOW_COLLECTOR_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <atomic>
#include <thread>
#include <mutex>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <tuple>
#include <map>
#include <string>
#include <utility>
#include <iostream>

namespace sflow {

#define SAMPLING_RATE 256
#define SFLOW_PORT 6343
#define BUFFER_SIZE 65535

  typedef std::tuple<std::string, std::string, int, int> FlowKey;   //srcIP, dstIP, srcPort, dstPort

  class SFlowCollector {
  public:
    SFlowCollector();
    ~SFlowCollector();

    template <typename T>
    static void hash_combine(std::size_t& seed, const T& val) {
      seed ^= std::hash<T>{}(val)+0x9e3779b9 + (seed << 6) + (seed >> 2);
    }

    struct FlowStats {
      uint64_t byte_count_current = 0;
      uint64_t byte_count_previous = 0;
      uint64_t avg_rate = 0;
    };


    struct FlowInfo {
      // key -> agent_ip and input_port
      // value -> avg_flow_sending_rate, accumulated_byte_counts
      std::map<std::pair<std::string, int>, FlowStats> data;
      uint64_t estimated_flow_sending_rate;
    };

    struct FlowKeyHash {
      std::size_t operator()(const FlowKey& key) const {
        std::size_t seed = 0;
        hash_combine(seed, std::get<0>(key));
        hash_combine(seed, std::get<1>(key));
        hash_combine(seed, std::get<2>(key));
        hash_combine(seed, std::get<3>(key));
        return seed;
      }
    };

    struct CounterInfo {
      time_t last_report_time;
      uint64_t last_received_input_octets;
      uint64_t last_received_output_octets;
    };

    std::mutex m_statusMutex;
    std::unordered_map<FlowKey, FlowInfo, FlowKeyHash> m_flowTable;
    // key -> agent_ip and port
    // value -> last_report_time, last_received_input_octets and last_received_output_octets, ...
    std::map<std::pair<uint32_t, uint32_t>, CounterInfo> m_counterReports;

    void start();
    void stop();

  private:
    std::string ipToString(uint32_t ip);
    std::string ourIpToString(uint32_t ip_front, uint32_t ip_back);
    void calAvgFlowSendingRates();
    void initSocket();
    void run();
    void handlePacket(char *buffer);


    std::atomic<int> m_sockfd{-1};
    std::atomic<bool> m_running{false};

    std::thread m_pktRcvThread;
    std::thread m_calAvgFlowSendingRateThread;
  };

} // namespace sflow

#endif // SFLOW_COLLECTOR_HPP
