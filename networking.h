#include "DataStructs/threadsafe.h"
#include "vertex.h"
#include <arpa/inet.h>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <list>
#include <mutex>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sstream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>
#include <random>
#define MAX_STREAM_SIZE 65536
#define MAX_DELAY 10
#define REQ_MSG 0
#define REG_MSG 1
#define FIN_MSG 2

class Server
{
  int domain;
  int protocol;
  int type;
  int listen_q;
  int s_socket;
  int port;
  int termination_round;
  struct sockaddr_in in_address;
  std::atomic_bool scan_buffer_thread_ready;
  Unordered_map<std::string, Vertex> vertex_map; // Vertex hash to vertex object

public:
  std::atomic_bool is_up;
  const static int MAXSIZE = 1024;

public:
  Server() = default;
  Server(const Server &source) = default;
  Server &operator=(Server &source) = default;
  Server(int domain, int type, int protocol, int port, int listen_q, int termination_round);
  Server(Server &&source) noexcept;
  Server &operator=(Server &&source) noexcept;

  int c_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
  int c_epoll_create(int flags);
  void c_epoll_ctl(int epfd, int op, int fd, epoll_event *event);
  int c_epoll_wait(int epfd, epoll_event *events, int maxevents, int timeout);
  void start(int predicted_connections, std::list<Vertex> &buffer, std::list<std::pair<uint64_t, Vertex>> &requests,
             std::mutex &mutex, std::condition_variable &cv, std::string id);
  void add_to_map(const std::string &key, Vertex &value);
  void set_thread_ready(bool state);
  bool get_thread_flag();
  int getport();
  void close_socket();
  Vertex get_vertex(std::string hash_value);
  bool find_in_map(const std::string &vertex_hash, Vertex *vertex);
};

class Client
{
  int domain;
  int protocol;
  int type;
  std::vector<uint64_t> peers;
  int termination_round;

public:
  std::vector<int> c_sockets;
  std::unordered_map<uint64_t, int> socket_map; // Peer ID to socket descriptor

public:
  Client() = default;
  Client(const Client &source) = default;
  Client &operator=(Client &source) = default;
  Client(int domain, int type, int protocol, int connections, int termination_round);
  Client(Client &&source) noexcept;
  Client &operator=(Client &&source) noexcept;

  void connect_to(std::vector<std::pair<std::string, int>> &servers);
  void c_connect(int fd, const sockaddr *addr, socklen_t len);
  void close_sockets();
  // void broadcast(const void *data, size_t size, int round, uint64_t myid);
  int get_socket(uint64_t peer_ID);
  uint64_t get_peer_id_at(int index);
};

int c_socket(int domain, int type, int protocol);
void c_sendn(int sockfd, char *buf, size_t len, int flags, std::random_device &device);
void c_recvn(int sockfd, char *buf, size_t len, int flags, std::vector<Vertex> &vertices);
int examine_msg(int socket, char *buffer, int len, uint32_t *bytes);
void process_request(int socket, char *buffer, std::string &hash, uint64_t *peer_ID);
void attach_length_to_msg(char *buffer, uint32_t msg_len);
char *dynamic_copy(char *buffer, size_t len);