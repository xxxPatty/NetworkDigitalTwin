#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>
#include <unordered_map>
#include <tuple>
#include <boost/functional/hash.hpp>
#include <utility>
#include <map>
#include <cstdint>
#include <thread>
#include <mutex>

using namespace std;

#define SAMPLING_RATE 256
#define SFLOW_PORT 6343
#define BUFFER_SIZE 1500


  mutex flow_table_mutex;


  typedef tuple<string, string, int, int> FlowKey;   //srcIP, dstIP, srcPort, dstPort

  template <typename T>
  inline void hash_combine(std::size_t& seed, const T& val) {
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
    map<pair<string, int>, FlowStats> data;
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


  unordered_map<FlowKey, FlowInfo, FlowKeyHash> flow_table;

  // Helper to convert 4-byte IP address to string
  string ipToString(uint32_t ip) {
    struct in_addr addr;
    addr.s_addr = ip;
    return string(inet_ntoa(addr));
  }

  string ourIpToString(uint32_t ip_front, uint32_t ip_back) {
    string res;
    res = to_string((ip_front & 65535) >> 8) + "." +
      to_string(ip_front & 255) + "." + to_string(ip_back >> 24) +
      "." + to_string((ip_back >> 16) & 255);
    return res;
  }

  void calAvgFlowSendingRates() {
    while (true) {
      this_thread::sleep_for(chrono::seconds(1));
      lock_guard<mutex> lock(flow_table_mutex);
      for (auto& [flow_key, info] : flow_table) {
        uint64_t avg_flow_sending_rate_temp = 0;
        int hops_counter = 0;
        for (auto& [link_key, stats] : info.data) {
          uint64_t bytes = stats.byte_count_current;

          stats.byte_count_previous = bytes;
          stats.byte_count_current = 0;
          stats.avg_rate = bytes * 8 * SAMPLING_RATE;
          avg_flow_sending_rate_temp += stats.avg_rate;
          if (stats.avg_rate != 0) {
            hops_counter++;
          }

          // cout << "LineKey: "
          // << "Agent IP: " << link_key.first << " Input Port: " << link_key.second 
          // << endl << endl;

          cout << "stats.avg_rate:  " << stats.avg_rate << " bits/s" << endl;
          // cout << "-------------------------------" << endl;
        }
        uint64_t estimated_flow_sending_rate = avg_flow_sending_rate_temp / hops_counter;
        info.estimated_flow_sending_rate = estimated_flow_sending_rate;
        cout << "FlowKey: "
          << get<0>(flow_key) << ":" << get<2>(flow_key) << " → "
          << get<1>(flow_key) << ":" << get<3>(flow_key)
          << endl;
        cout << "Estimated flow sending rate: " << estimated_flow_sending_rate << endl << endl;
      }
      cout << "===================================" << endl;
    }
  }


int main() {
  int sockfd;
  char buffer[BUFFER_SIZE];
  struct sockaddr_in servaddr {}, cliaddr{};
  socklen_t len = sizeof(cliaddr);

  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0) {
    perror("socket creation failed");
    return 1;
  }

  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = INADDR_ANY;
  servaddr.sin_port = htons(SFLOW_PORT);

  if (bind(sockfd, (const struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
    perror("bind failed");
    return 1;
  }

  cout << "Listening for sFlow on UDP port " << SFLOW_PORT << "...\n";

  std::thread rate_calc_thread(calAvgFlowSendingRates);
  rate_calc_thread.detach();


  while (true) {
    int n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0,
      (struct sockaddr*)&cliaddr, &len);
    if (n <= 0) continue;

    // cout << "\n🔹 Received sFlow packet (" << n << " bytes)\n";

    uint32_t* data = (uint32_t*)buffer;

    uint32_t version = ntohl(data[0]);
    if (version != 5) {
      cout << "Unsupported sFlow version: " << version << "\n";
      continue;
    }

    uint32_t agent_ip = data[2];
    uint32_t sub_agent_id = ntohl(data[3]);
    uint32_t sequence_number = ntohl(data[4]);
    uint32_t uptime = ntohl(data[5]);
    uint32_t sample_count = ntohl(data[6]);

    string agent_ip_str = ipToString(agent_ip);

    // cout << "Version:        " << version << "\n";
    // cout << "Agent Address:  " << agent_ip_str << "\n";
    // cout << "Sub Agent ID:   " << sub_agent_id << "\n";
    // cout << "Seq Number:     " << sequence_number << "\n";
    // cout << "Uptime (ms):    " << uptime << "\n";
    // cout << "Sample Count:   " << sample_count << "\n";

    uint32_t index = 8;
    for (uint32_t i = 0; i < sample_count; i++) {
      uint32_t sample_len = ntohl(data[index]);
      uint32_t input_port = ntohl(data[index + 6]);
      uint32_t frame_length = ntohl(data[index + 12]);
      uint32_t protocol = ntohl(data[index + 20]) & 255;
      uint32_t src_ip_front = ntohl(data[index + 21]);
      uint32_t src_ip_back = ntohl(data[index + 22]);
      uint32_t dst_ip_front = ntohl(data[index + 22]);
      uint32_t dst_ip_back = ntohl(data[index + 23]);
      uint16_t src_port = ntohl(data[index + 23]) & 65535;
      uint16_t dst_port = ntohl(data[index + 24]) >> 16;

      index += (sample_len / 4 + 2);

      // cout << "Sample Len:       " << sample_len << endl;
      // cout << "Input port:       " << input_port << endl;
      // cout << "Source IP:        "
      //           << ourIpToString(src_ip_front, src_ip_back) << endl;
      // cout << "Destination IP:   "
      //           << ourIpToString(dst_ip_front, dst_ip_back) << endl;
      // cout << "Source Port:      " << src_port << endl;
      // cout << "Destination Port: " << dst_port << endl;

      // cout << "Protocol:            "  << protocol << endl;

      if (protocol == 6) {  // TCP
        FlowKey key = make_tuple(ourIpToString(src_ip_front, src_ip_back), ourIpToString(dst_ip_front, dst_ip_back), src_port, dst_port);
        auto item = flow_table.find(key);
        if (item != flow_table.end()) {
          uint64_t old_accumulated_byte_count = flow_table[key].data[make_pair(agent_ip_str, int(input_port))].byte_count_current;
          flow_table[key].data[make_pair(agent_ip_str, int(input_port))].byte_count_current = old_accumulated_byte_count + uint64_t(frame_length);
        }
        else {
          flow_table[key].data[make_pair(agent_ip_str, int(input_port))] = { 0, 0 };
        }
        // cout << "Accumulated byte count:  " << flow_table[key].data[make_pair(agent_ip_str, int(input_port))].byte_count_current << endl;
      }
    }
  }

  close(sockfd);
  return 0;
}
