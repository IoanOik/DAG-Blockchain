#include "networking.h"

// Server constructor

Server::Server(int domain, int type, int protocol, int port, int listen_q, int termination_round)
    : domain(domain), type(type), protocol(protocol), port(port), listen_q(listen_q),
      termination_round(termination_round), scan_buffer_thread_ready(false), is_up(true)
{
    this->s_socket = c_socket(domain, type, protocol);
    this->in_address.sin_family = domain;
    this->in_address.sin_port = htons(port);
    this->in_address.sin_addr.s_addr = INADDR_ANY;

    if (bind(this->s_socket, (struct sockaddr *)&this->in_address, sizeof(this->in_address)) < 0)
    {
        std::cerr << "Error binding socket: " << strerror(errno) << std::endl;
        close(this->s_socket);
        exit(EXIT_FAILURE);
    }
    int sendbuf, recvbuf;
    socklen_t len;
    getsockopt(this->s_socket, SOL_SOCKET, SO_SNDBUF, &sendbuf, &len);
    getsockopt(this->s_socket, SOL_SOCKET, SO_RCVBUF, &recvbuf, &len);

    std::ofstream file("Server_limits.txt", std::ios::app);
    file << sendbuf << " " << recvbuf << std::endl;
    file.close();

    listen(this->s_socket, this->listen_q);
}

// Server move constructor

Server::Server(Server &&source) noexcept
{
    domain = source.domain;
    protocol = source.protocol;
    type = source.type;
    listen_q = source.listen_q;
    s_socket = source.s_socket;
    port = source.port;
    termination_round = source.termination_round;
    in_address = source.in_address;
    scan_buffer_thread_ready.exchange(source.scan_buffer_thread_ready.load());

    source.domain = -1;
    source.protocol = -1;
    source.type = -1;
    source.listen_q = -1;
    source.s_socket = -1;
    source.port = -1;
    source.termination_round = -1;
    source.scan_buffer_thread_ready.store(false);
    std::memset(&source.in_address, 0, sizeof(source.in_address));
}

// server move assignment operator

Server &Server::operator=(Server &&source) noexcept
{
    if (this != &source)
    {
        domain = source.domain;
        protocol = source.protocol;
        type = source.type;
        listen_q = source.listen_q;
        s_socket = source.s_socket;
        port = source.port;
        termination_round = source.termination_round;
        std::memset(&in_address, 0, sizeof(struct sockaddr_in));
        in_address = source.in_address;
        scan_buffer_thread_ready.exchange(source.scan_buffer_thread_ready.load());

        source.domain = -1;
        source.protocol = -1;
        source.type = -1;
        source.listen_q = -1;
        source.s_socket = -1;
        source.port = -1;
        source.termination_round = -1;
        source.scan_buffer_thread_ready.store(false);
        std::memset(&source.in_address, 0, sizeof(struct sockaddr_in));
    }
    return *this;
}

// Server methods

int Server::getport()
{
    return this->port;
}

void Server::close_socket()
{
    close(this->s_socket);
}

void Server::start(int predicted_connections, std::list<Vertex> &buffer,
                   std::list<std::pair<uint64_t, Vertex>> &requests, std::mutex &mutex, std::condition_variable &cv,
                   std::string id)
{
    printf("Server up and running! : %d\n", this->port);
    // std::ofstream server_file;
    std::string str = "_server";
    std::string filename = id + str;

    std::unique_lock<std::mutex> u_lock(mutex, std::defer_lock);
    std::list<Vertex> temp_list;
    std::list<std::pair<uint64_t, Vertex>> temp_request_list;
    int crowd = predicted_connections + 1;
    double max_byzantines = std::ceil((double)crowd / 3 - 1);
    size_t majority = (size_t)(2 * max_byzantines + 1);

    int ep_fd = c_epoll_create(0);
    struct epoll_event event, events[Server::MAXSIZE];
    event.events = EPOLLIN;
    event.data.fd = this->s_socket;
    c_epoll_ctl(ep_fd, EPOLL_CTL_ADD, this->s_socket, &event);

    std::vector<Vertex> vertices;
    char byte_stream[MAX_STREAM_SIZE];
    int msg_type;
    uint32_t bytes_to_recv = 0;

    std::ofstream file;
    std::string name = id + "_error";

    while (this->is_up)
    {
        // printf("WAITING: %s\n", id.c_str());
        // sleep(3);
        int nfds = c_epoll_wait(ep_fd, events, Server::MAXSIZE, 0);

        // if (nfds == 0)
        // {
        //     std::ofstream file;
        //     file.open(name, std::ios::app);
        //     file << "Empty epoll" << std::endl;
        //     file.close();
        // }

        for (int i = 0; i < nfds; i++)
        {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            if ((ev & EPOLLIN) == EPOLLIN)
            {
                if (fd == this->s_socket)
                {
                    /* NEW CONNECTION REQUEST */

                    struct sockaddr_in client_addr;
                    socklen_t len = sizeof(client_addr);

                    int client_handler_fd = c_accept(this->s_socket, (struct sockaddr *)&client_addr, &len);
                    event.events = EPOLLIN;
                    event.data.fd = client_handler_fd;
                    c_epoll_ctl(ep_fd, EPOLL_CTL_ADD, client_handler_fd, &event);
                }
                else
                {
                    /* MESSAGE FROM WITHIN THE NETWORK */

                    /* Checking if I have a request message */

                    msg_type = examine_msg(fd, byte_stream, sizeof(byte_stream), &bytes_to_recv);

                    if (msg_type == REQ_MSG)
                    {
                        /* This is a request message */
                        uint64_t peerID;
                        std::string vertex_hash;
                        Vertex requested_vertex;
                        process_request(fd, byte_stream, vertex_hash, &peerID);

                        if (this->vertex_map.find(vertex_hash, &requested_vertex))
                        {
                            // requested_vertex = this->vertex_map[vertex_hash];
                            temp_request_list.emplace_front(peerID, requested_vertex);
                            printf("I can answer a request for round %d\n", requested_vertex.get_round());
                        }
                        else
                            printf("I cannot answer this request! %s\n", id.c_str());
                    }
                    else if (msg_type == REG_MSG)
                    {
                        /* I received a regular vertex message */

                        c_recvn(fd, byte_stream, ntohl(bytes_to_recv), MSG_WAITALL, vertices);

                        // server_file.open(filename, std::ios::app);
                        for (Vertex v : vertices)
                        {
                            if (v.get_refs().size() >= majority || (v.get_round() == 1))
                            {
                                if (!vertex_map.find(v.hash(), nullptr))
                                {
                                    // server_file << v.get_round() << ":" << v.get_pid() << "\n";
                                    temp_list.push_front(v);
                                    vertex_map.insert(v.hash(), v);
                                }
                            }
                        }
                        // server_file.close();
                        vertices.clear();
                    }
                    else if (msg_type == FIN_MSG)
                    {
                        /* peer exited */
                        c_epoll_ctl(ep_fd, EPOLL_CTL_DEL, fd, nullptr);
                        close(fd);
                    }
                }
            }
        }
        if (!temp_list.empty() || !temp_request_list.empty() || nfds == 0)
        {
            int k = 0;
            while (!this->scan_buffer_thread_ready)
            {
                // Busy waiting, not optimal but maybe necessary
                k = 1;
                // std::this_thread::yield();
                sleep(1);
                printf("BUSY WAITING : %s\n", id.c_str());
            }
            if (k)
            {
                printf("STOPPED BUSY WAITING : %s\n", id.c_str());
            }

            this->scan_buffer_thread_ready = false;

            // if (!temp_list.empty() || !temp_request_list.empty())
            // {

            // }

            u_lock.lock();
            buffer.insert(buffer.end(), temp_list.begin(), temp_list.end());
            requests.insert(requests.end(), temp_request_list.begin(), temp_request_list.end());
            u_lock.unlock();

            cv.notify_one();
            temp_list.clear();
            temp_request_list.clear();
        }
    }
    // server_file.open(filename, std::ios::app);
    // server_file << "Server stopped running!\n";
    // server_file.close();
    close(this->s_socket);
    close(ep_fd);
    puts("Server stopped running!");
    u_lock.release();
}

void Server::add_to_map(const std::string &key, Vertex &value)
{
    this->vertex_map.insert(key, value);
}

void Server::set_thread_ready(bool state)
{
    this->scan_buffer_thread_ready.store(state);
}

bool Server::get_thread_flag()
{
    return this->scan_buffer_thread_ready.load();
}

int Server::c_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    int fd = accept(sockfd, addr, addrlen);
    if (fd < 0)
    {
        std::cerr << "Error accepting connection: " << strerror(errno) << std::endl;
        exit(EXIT_FAILURE);
    }
    return fd;
}

int Server::c_epoll_create(int flags)
{
    int epfd = epoll_create1(flags);
    if (epfd < 0)
    {
        std::cerr << "Epoll create error: " << strerror(errno) << std::endl;
        exit(EXIT_FAILURE);
    }
    return epfd;
}

void Server::c_epoll_ctl(int epfd, int op, int fd, epoll_event *event)
{
    int a = epoll_ctl(epfd, op, fd, event);
    if (a < 0)
    {
        std::cerr << "Epoll control error: " << strerror(errno) << std::endl;
        exit(EXIT_FAILURE);
    }
}

int Server::c_epoll_wait(int epfd, epoll_event *events, int maxevents, int timeout)
{
    if (timeout > 0)
        timeout *= 1000;
    int nfds = epoll_wait(epfd, events, maxevents, timeout);
    if (nfds < 0)
    {
        std::cerr << "Epoll wait error: " << strerror(errno) << std::endl;
        exit(EXIT_FAILURE);
    }
    return nfds;
}

// Vertex Server::get_vertex(std::string hash_value)
// {
//     return this->vertex_map[hash_value];
// }

bool Server::find_in_map(const std::string &vertex_hash, Vertex *vertex)
{
    return this->vertex_map.find(vertex_hash, vertex);
}

// Client constructor

Client::Client(int domain, int type, int protocol, int connections, int termination_round)
    : domain(domain), type(type), protocol(protocol), termination_round(termination_round)
{
    this->c_sockets.reserve(connections);
}

// Client move constructor

Client::Client(Client &&source) noexcept
{
    domain = source.domain;
    protocol = source.protocol;
    type = source.type;
    c_sockets = std::move(source.c_sockets);

    source.domain = -1;
    source.protocol = -1;
    source.type = -1;
    source.c_sockets.clear();
}
// Client move assignment operator

Client &Client::operator=(Client &&source) noexcept
{
    if (this != &source)
    {
        c_sockets.clear();

        domain = source.domain;
        protocol = source.protocol;
        type = source.type;
        c_sockets = std::move(source.c_sockets);

        source.domain = -1;
        source.protocol = -1;
        source.type = -1;
        source.c_sockets.clear();
    }
    return *this;
}

// Client methods

void Client::connect_to(std::vector<std::pair<std::string, int>> &servers)
{

    std::ofstream file;
    std::string name = std::to_string(getpid()) + "_error_client";

    std::ifstream f;

    printf("\n\n");
    for (size_t i = 0; i < servers.size(); i++)
    {
        printf("%s %d\n", servers[i].first.c_str(), servers[i].second);
    }
    printf("\n\n");

    struct sockaddr_in peer_address;
    int new_socket;
    uint64_t value;

    for (int i = 0; i < servers.size(); i++)
    {
        std::memset(&peer_address, 0, sizeof(peer_address));
        peer_address.sin_family = this->domain;
        if (inet_aton(servers[i].first.c_str(), &peer_address.sin_addr) == 0)
        {
            file.open(name, std::ios::app);
            file << "inet_aton error for" << servers[i].first.data() << "\n";
            file.close();
        }

        peer_address.sin_port = htons(servers[i].second);

        new_socket = c_socket(this->domain, this->type, this->protocol);

        int sendbuf, recvbuf;
        socklen_t len;
        getsockopt(new_socket, SOL_SOCKET, SO_SNDBUF, &sendbuf, &len);
        getsockopt(new_socket, SOL_SOCKET, SO_RCVBUF, &recvbuf, &len);

        std::ofstream file("Client_limits.txt", std::ios::app);
        file << sendbuf << " " << recvbuf << std::endl;
        file.close();

        c_connect(new_socket, (struct sockaddr *)&peer_address, sizeof(peer_address));
        this->c_sockets.push_back(new_socket);

        f.open(std::to_string(servers[i].second));
        f >> value;
        f.close();

        this->socket_map[value] = new_socket;
        this->peers.push_back(value);
    }

    for (auto x : socket_map)
    {
        std::cout << x.first << " " << x.second << std::endl;
    }
}

void Client::c_connect(int fd, const sockaddr *addr, socklen_t len)
{
    int a = connect(fd, addr, len);
    if (a < 0)
    {
        std::cerr << "Error creating connection: " << strerror(errno) << std::endl;
        close_sockets();
        exit(EXIT_FAILURE);
    }
}

void Client::close_sockets()
{
    for (int socket : this->c_sockets)
        shutdown(socket, SHUT_WR);
    this->c_sockets.clear();
}

int Client::get_socket(uint64_t peer_ID)
{
    return this->socket_map[peer_ID];
}

uint64_t Client::get_peer_id_at(int index)
{
    return this->peers[index];
}

// Other functions

int c_socket(int domain, int type, int protocol)
{
    int fd = socket(domain, type, protocol);
    if (fd < 0)
    {
        std::cerr << "Error creating socket: " << strerror(errno) << std::endl;
        exit(EXIT_FAILURE);
    }
    return fd;
}

void c_sendn(int sockfd, char *buf, size_t len, int flags, std::random_device &device)
{
    int propability, duration;
    const char *ptr = buf;
    size_t bytes_sent;
    size_t bytes_left = len;
    // uintptr_t address = (uintptr_t)buf;
    // unsigned int seed = time(nullptr);
    // std::random_device rd;
    std::mt19937 gen(device());
    std::uniform_int_distribution<int> dis_one(1, 100);
    std::uniform_int_distribution<int> dis_two(0, MAX_DELAY * 1000);

    propability = dis_one(gen);
    if (propability <= 4)
    {
        free(buf);
        printf("----------message destroyed----------   %d\n", propability);
        return;
    }
    duration = dis_two(gen);
    std::this_thread::sleep_for(std::chrono::milliseconds(duration));
    // sleep(seconds);

    while (bytes_left > 0)
    {
        bytes_sent = send(sockfd, ptr, bytes_left, MSG_NOSIGNAL | flags);
        if (bytes_sent < 0)
        {
            std::cerr << "Error sending a message: " << strerror(errno) << std::endl;
            exit(EXIT_FAILURE);
        }
        bytes_left -= bytes_sent;
        ptr += bytes_sent;

        if (bytes_left > 0)
        {
            printf("BYTES SENT: %ld %d\n", bytes_sent, getpid());
            if (bytes_sent == -1)
            {
                std::cout << "Error sending a message: " << strerror(errno) << " " << errno << " " << len << std::endl;
                bytes_left = 0;
            }
        }
    }
    free(buf);
}

void c_recvn(int sockfd, char *buf, size_t len, int flags, std::vector<Vertex> &vertices)
{
    char chunk_buffer[MAX_STREAM_SIZE];
    int bytes_received;
    int segment_start = 0;
    int segment_size = 0;
    bool delimeter_found = false;
    Vertex v;

    std::memset(chunk_buffer, 0, sizeof(chunk_buffer));
    // std::cout << "LEN: " + std::to_string(len) << std::endl;
    //  std::cout << "FLAGS: " << flags << std::endl;

    while (!delimeter_found)
    {
        std::memset(buf, 0, MAX_STREAM_SIZE);
        bytes_received = recv(sockfd, buf, len, flags);

        if (bytes_received != len)
        {
            printf("RECEIVING ERROR!!! %d %ld\n", bytes_received, len);
            exit(EXIT_FAILURE);
        }
        if (bytes_received < 0)
        {
            std::cerr << "Error receiving a message: " << strerror(errno) << std::endl;
            exit(EXIT_FAILURE);
        }

        for (int byte_counter = 0; byte_counter < bytes_received; byte_counter++)
        {
            if (buf[byte_counter] == DELIM && buf[byte_counter + 3] == DELIM) // new
            {
                byte_counter += 3; // new
                if (len == 1126 || len == 1382)
                {
                    printf("COUNTER: %d\n", byte_counter);
                }

                segment_size = byte_counter - segment_start + 1;
                std::memcpy(chunk_buffer, buf + segment_start, segment_size);
                segment_start = byte_counter + 1;

                v.deserialize_data(chunk_buffer, sizeof(chunk_buffer));
                vertices.emplace_back(v);
                std::memset(chunk_buffer, 0, sizeof(chunk_buffer));

                if (byte_counter + 1 < bytes_received)
                {
                    delimeter_found = false;
                }
                else
                {
                    delimeter_found = true;
                    break;
                }
            }
        }
    }
}

int examine_msg(int socket, char *buffer, int len, uint32_t *bytes)
{
    std::memset(buffer, 0, len);
    int count = recv(socket, buffer, len, MSG_PEEK);
    if (count == 0)
    {
        std::memset(buffer, 0, len);
        return FIN_MSG;
    }
    else if (count < 0)
    {
        std::cerr << "Error receiving a message: " << strerror(errno) << std::endl;
        exit(EXIT_FAILURE);
    }

    if (buffer[0] == PREFIX)
    {
        std::memset(buffer, 0, len);
        return REQ_MSG;
    }
    else
    {
        std::memcpy(bytes, buffer, sizeof(uint32_t));
        std::memset(buffer, 0, len);
        return REG_MSG;
    }
}

void process_request(int socket, char *buffer, std::string &hash, uint64_t *peer_ID)
{
    int length = sizeof(uint64_t) + HEX_HASH + 1;
    if (recv(socket, buffer, length, MSG_WAITALL) < 0)
    {
        std::cerr << "Error receiving a message: " << strerror(errno) << std::endl;
        exit(EXIT_FAILURE);
    }
    hash.assign(buffer + 1, HEX_HASH);
    std::memcpy(peer_ID, buffer + HEX_HASH + 1, sizeof(uint64_t));
    *peer_ID = be64toh(*peer_ID);
}

void attach_length_to_msg(char *buffer, uint32_t msg_len)
{
    uint32_t len = htonl(msg_len);
    std::memcpy(buffer, &len, sizeof(len));
    if (len == 0)
    {
        printf("THE PROBLEM IS ON SEND!!!!\n");
        exit(EXIT_FAILURE);
    }
}

char *dynamic_copy(char *buffer, size_t len)
{
    char *ptr = (char *)malloc(len * sizeof(char));
    std::memcpy(ptr, buffer, len);
    return ptr;
}