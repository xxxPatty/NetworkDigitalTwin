#include "SFlowCollector.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <boost/functional/hash.hpp>
#include <chrono>
#include <ctime>
#include <cstdint>
#include <netinet/ip.h>
#include <netinet/in.h>

using namespace std;

namespace sflow {

SFlowCollector::SFlowCollector() : m_sockfd(-1) {}

SFlowCollector::~SFlowCollector() { stop(); }

void SFlowCollector::start() {
  initSocket();
  // Launch the main loop in a separate thread.;
  this->m_running.store(true);
  m_pktRcvThread = thread(&SFlowCollector::run, this);
  m_calAvgFlowSendingRateThread = thread(&SFlowCollector::calAvgFlowSendingRates, this);
}

void SFlowCollector::stop() {
  if (m_sockfd != -1) {
    ::close(m_sockfd);
    m_sockfd = -1;
  }
  if (m_pktRcvThread.joinable()) {
    m_pktRcvThread.join();
  }
  if (m_calAvgFlowSendingRateThread.joinable()) {
    m_calAvgFlowSendingRateThread.join();
  }
}

void SFlowCollector::initSocket() {
  // TODO: Should be Real init Socket
  // Not be placed in ::run()
  // lock_guard<mutex> lock(m_socketMutex);
  // m_sockfd = ::socket(AF_INET, SOCK_DGRAM, 0);
  // if (m_sockfd < 0) {
  //   perror("socket");
  //   exit(EXIT_FAILURE);
  // }
  // sockaddr_in addr{};
  // addr.sin_family = AF_INET;
  // addr.sin_port = htons(SFLOW_PORT);
  // addr.sin_addr.s_addr = INADDR_ANY;

  // if (::bind(m_sockfd, (const struct sockaddr*)&addr, sizeof(addr)) < 0) {
  //   perror("bind");
  //   exit(EXIT_FAILURE);
  // }
  // cout << "Listening for sFlow on UDP port " << SFLOW_PORT << "...\n";
}

void SFlowCollector::run() {
  m_sockfd = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (m_sockfd < 0) {
    perror("socket");
    exit(EXIT_FAILURE);
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(SFLOW_PORT);
  addr.sin_addr.s_addr = INADDR_ANY;

  if (::bind(m_sockfd, (const struct sockaddr*)&addr, sizeof(addr)) < 0) {
    perror("bind");
    exit(EXIT_FAILURE);
  }
  cout << "Listening for sFlow on UDP port " << SFLOW_PORT << "...\n";

  char buffer[BUFFER_SIZE];
  sockaddr_in clientAddr{};
  socklen_t addrLen = sizeof(clientAddr);

  while (m_running) {
    ssize_t len =
        ::recvfrom(m_sockfd, buffer, BUFFER_SIZE, 0,
                   reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
    // cout << len << endl;
    if (len > 0) {
      handlePacket(buffer);
    }
    // Sleep briefly to avoid busy looping.
    this_thread::sleep_for(chrono::milliseconds(10));
  }
}

void SFlowCollector::handlePacket(char* buffer) {
  uint32_t* data = (uint32_t*)buffer;

  uint32_t version = ntohl(data[0]);

  if (version != 5) {
    cout << "Unsupported sFlow version: " << version << "\n";
    return;
  }

  uint32_t agent_ip = data[2];
  uint32_t sub_agent_id = ntohl(data[3]);
  uint32_t sequence_number = ntohl(data[4]);
  uint32_t uptime = ntohl(data[5]);
  uint32_t sample_count = ntohl(data[6]);

  string agent_ip_str = ipToString(agent_ip);

  // cout << "Version:        " << version << "\n";
  cout << "Agent Address:  " << agent_ip_str << "\n";
  // cout << "Sub Agent ID:   " << sub_agent_id << "\n";
  // cout << "Seq Number:     " << sequence_number << "\n";
  // cout << "Uptime (ms):    " << uptime << "\n";
  // cout << "Sample Count:   " << sample_count << "\n";

  uint32_t index = 7;
  for (uint32_t i = 0; i < sample_count; i++) {
    uint32_t sample_type = ntohl(data[index]);
    if (sample_type == 2) { // Counter sample
      uint32_t sample_len = ntohl(data[index + 1]);
      uint32_t interface_index = ntohl(data[index + 4 + 15 + 3]);
      uint32_t interface_speed_front = ntohl(data[index + 4 + 15 + 5]);
      uint32_t interface_speed_back = ntohl(data[index + 4 + 15 + 6]);
      uint64_t interface_speed = interface_speed_front << 32 | interface_speed_back;
      uint32_t input_octets_front = ntohl(data[index + 4 + 15 + 9]);
      uint32_t input_octets_back = ntohl(data[index + 4 + 15 + 10]);
      uint64_t input_octets = input_octets_front << 32 | input_octets_back;
      uint32_t output_octets_front = ntohl(data[index + 4 + 15 + 17]);
      uint32_t output_octets_back = ntohl(data[index + 4 + 15 + 18]);
      uint64_t output_octets = output_octets_front << 32 | output_octets_back;

      index += (sample_len / 4 + 2);

      cout << "Sample Len:      " << sample_len << endl;
      cout << "Interface Index: " << interface_index << endl;
      cout << "Interface Speed: " << interface_speed << endl;
      cout << "Input Octets:    " << input_octets << endl;
      cout << "Output Octets:   " << output_octets << endl;
      cout << "-------------------------------" << endl;

      pair<uint32_t, uint32_t> agent_ip_and_port(agent_ip, interface_index);
      time_t now = time(NULL);
      time_t interval = now - m_counterReports[agent_ip_and_port].last_report_time;
      if (interval <= 0) continue;
      uint64_t input_octets_diff = input_octets - m_counterReports[agent_ip_and_port].last_received_input_octets;
      uint64_t output_octets_diff = output_octets - m_counterReports[agent_ip_and_port].last_received_output_octets;
      if (input_octets_diff >= 0) {
        uint64_t avg_out = input_octets_diff / interval;
        cout << "Average Link Usage (Out):  " << avg_out << endl;
      }
      if (output_octets_diff >= 0) {
        uint64_t avg_in = output_octets_diff / interval;
        cout << "Average Link Usage (In):   " << avg_in << endl;
      }
      cout << "~~~~~~~~~~~~~~~~~~~~~~~~~~" << endl;

      m_counterReports[agent_ip_and_port].last_report_time = now;
      m_counterReports[agent_ip_and_port].last_received_input_octets = input_octets;
      m_counterReports[agent_ip_and_port].last_received_output_octets = output_octets;

      // TODO: store link bandwidth usage info to edge property

    } else if (sample_type == 1) {  // Flow sample
      uint32_t sample_len = ntohl(data[index + 1]);
      uint32_t input_port = ntohl(data[index + 7]);
      uint32_t frame_length = ntohl(data[index + 13]);
      uint32_t protocol = ntohl(data[index + 21]) & 255;
      uint32_t src_ip_front = ntohl(data[index + 22]);
      uint32_t src_ip_back = ntohl(data[index + 23]);
      uint32_t dst_ip_front = ntohl(data[index + 23]);
      uint32_t dst_ip_back = ntohl(data[index + 24]);
      uint16_t src_port = ntohl(data[index + 24]) & 65535;
      uint16_t dst_port = ntohl(data[index + 25]) >> 16;
  
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
      // cout << "-------------------------------" << endl;
  
      if (protocol == 6) {  // TCP
        FlowKey key = make_tuple(ourIpToString(src_ip_front, src_ip_back),
                                 ourIpToString(dst_ip_front, dst_ip_back),
                                 src_port, dst_port);
        auto item = m_flowTable.find(key);
        if (item != m_flowTable.end()) {
          uint64_t old_accumulated_byte_count =
              m_flowTable[key]
                  .data[make_pair(agent_ip_str, int(input_port))]
                  .byte_count_current;
          m_flowTable[key]
              .data[make_pair(agent_ip_str, int(input_port))]
              .byte_count_current =
              old_accumulated_byte_count + uint64_t(frame_length);
        } else {
          m_flowTable[key].data[make_pair(agent_ip_str, int(input_port))] = {0, 0};
        }
        // cout << "Accumulated byte count:  " <<
        // m_flowTable[key].data[make_pair(agent_ip_str,
        // int(input_port))].byte_count_current << endl;

        // TODO: store flow info to edge property
      }
    }
  }
}


void SFlowCollector::calAvgFlowSendingRates() {
  while (true) {
    this_thread::sleep_for(chrono::seconds(1));
    lock_guard<mutex> lock(m_statusMutex);
    for (auto& [flow_key, info] : m_flowTable) {
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
        } else {
          continue;
        }

        // cout << "LineKey: "
        // << "Agent IP: " << link_key.first << " Input Port: " <<
        // link_key.second
        // << endl << endl;

        cout << "stats.avg_rate:  " << stats.avg_rate << " bits/s" << endl;
        // cout << "-------------------------------" << endl;
      }
      
      if (hops_counter == 0) continue;
      uint64_t estimated_flow_sending_rate =
          avg_flow_sending_rate_temp / hops_counter;
      info.estimated_flow_sending_rate = estimated_flow_sending_rate;
      cout << "FlowKey: " << get<0>(flow_key) << ":" << get<2>(flow_key)
           << " → " << get<1>(flow_key) << ":" << get<3>(flow_key) << endl;
      cout << "Estimated flow sending rate: " << estimated_flow_sending_rate
           << endl
           << endl;
    }
    cout << "===================================" << endl;
  }
}

string SFlowCollector::ipToString(uint32_t ip) {
  struct in_addr addr;
  addr.s_addr = ip;
  return string(inet_ntoa(addr));
}

string SFlowCollector::ourIpToString(uint32_t ip_front, uint32_t ip_back) {
  string res;
  res = to_string((ip_front & 65535) >> 8) + "." + to_string(ip_front & 255) +
        "." + to_string(ip_back >> 24) + "." + to_string((ip_back >> 16) & 255);
  return res;
}

}  // namespace sflow
