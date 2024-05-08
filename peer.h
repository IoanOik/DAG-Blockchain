#include "networking.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cryptopp/hex.h>
#include <cryptopp/sha.h>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <list>
#include <omp.h>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#define WAVE 4
#define HALF_WAVE WAVE / 2

class Peer
{
private:
  int round = 0;
  int crowd;
  int last_round;
  int byzantines;
  uint64_t id;
  Server server;
  Client client;
  std::list<Vertex> buffer;
  std::list<std::pair<uint64_t, Vertex>> requests;
  std::mutex mutex;
  std::unique_lock<std::mutex> u_lock;
  std::condition_variable cv;
  std::thread scan_buffer_thread;
  std::thread server_thread;
  std::vector<std::pair<std::string, int>> known_peers;
  Unordered_set<uint64_t> black_list;
  std::atomic_int requests_asked;
  bool fail;
  std::vector<std::unordered_set<std::string>> dag_image;
  Threadpool pool;
  // Threadpool req_pool;
  // Threadpool deliver_pool;
  std::random_device rd;

public:
  Peer() = default;
  Peer(int domain, int type, int protocol, int port, int listen_q, int crowd, int last_round, bool fail);
  Peer(Peer &&source) noexcept;
  // Peer &operator=(Peer &&source) noexcept;
  void setup_network(const char *filename);
  void buffer_scan_move_round();
  bool mutual_history(int v_round, const std::vector<std::string> &v_refs, std::unordered_set<std::string> &temp_set,
                      uint64_t pid);
  void start_thread();
  void join_threads();
  uint64_t getid();
  void close_server();
  void send_missing_vertices(int peer_socket, std::unordered_set<std::string> &reference_set);
  void dag_scan(int end, int start = 0);
  void check_round(int round);
  void make_request(std::string hash, int owner);
  void deliver_request(std::pair<uint64_t, Vertex> request);
  void total_order(int end, int start);
  void hash_set(int index, std::string &output);
  void hash_dag(std::string &output);
  void write_record(int *requests_answered, int *missing_vertices, bool help, int times_i_need_help);
  void broadcast(char *data, size_t size, int round, uint64_t myid);
};