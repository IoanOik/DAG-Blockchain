#include "peer.h"

// Peer constructor

Peer::Peer(int domain, int type, int protocol, int port, int listen_q, int crowd, int last_round, bool fail)
    : server(domain, type, protocol, port, listen_q, last_round), client(domain, type, protocol, crowd - 1, last_round),
      u_lock(mutex, std::defer_lock), crowd(crowd), last_round(last_round), fail(fail),
      pool(160) // req_pool(60), deliver_pool(60)
{
    this->id = std::chrono::system_clock::now().time_since_epoch().count();
    this->requests_asked = 0;
    this->known_peers.reserve(crowd - 1);
    this->byzantines = std::ceil((double)this->crowd / 3 - 1);

    std::ofstream file;
    std::string filename = std::to_string(this->server.getport());
    std::string records_filename = "records/" + std::to_string(this->id) + "_records.csv";
    file.open(filename, std::ios::trunc);
    file << this->id << "\n";
    file.close();
    file.open(records_filename);
    file << "TIMESTAMP,CURRENT ROUND,TOTAL VERTICES,TOTAL ABSENT VERTICES,TOTAL MESSAGES,TIMES I NEEDED "
            "HELP,VERTICES ON ROUND,CURRENTLY ASKING FOR HELP,ANSWERED REQUESTS,ADDITIONAL VERTICES MESSAGES,BROADCAST MESSAGES,REQUEST "
            "MESSAGES,KNOWN FAILED VALIDATORS\n";
    file.close();
    this->server_thread =
        std::thread(&Server::start, &this->server, crowd - 1, std::ref(this->buffer), std::ref(this->requests),
                    std::ref(this->mutex), std::ref(this->cv), std::to_string(this->id));
}

// Peer move costructor

Peer::Peer(Peer &&source) noexcept
{
    // std::unique_lock<std::mutex> temp_lck(source.mutex);
    round = source.round;
    crowd = source.crowd;
    last_round = source.last_round;
    id = source.id;
    server = std::move(source.server);
    client = std::move(source.client);
    buffer = std::move(source.buffer);
    // source.u_lock.unlock();
    u_lock = std::move(source.u_lock);
    known_peers = std::move(source.known_peers);
    dag_image = std::move(source.dag_image);
    scan_buffer_thread = std::move(source.scan_buffer_thread);
    server_thread = std::move(source.server_thread);
    requests = std::move(source.requests);
    requests_asked.exchange(source.requests_asked);
    // source.u_lock.release();
    //  temp_lck.unlock();
    //   mutex = std::move(source.mutex);
    //   mutex = std::move(source.mutex);
}

// // Peer move assignment operator
// Peer &Peer::operator=(Peer &&source) noexcept
// {
// }

// Peer methods

void Peer::setup_network(const char *filename)
{
    std::string line, ip_address;
    std::ifstream fin(filename);
    int port;

    if (!fin.is_open())
    {
        std::cout << "Error openning the text file!" << std::endl;
        exit(EXIT_FAILURE);
    }

    while (std::getline(fin, line))
    {
        std::istringstream stream(line);
        stream >> ip_address >> port;
        if (this->server.getport() != port)
            this->known_peers.emplace_back(ip_address, port);
    }
    fin.close();
    this->client.connect_to(known_peers);
}

void Peer::buffer_scan_move_round()
{
    Vertex new_vertex;
    std::string vertex_hash;
    std::list<Vertex>::iterator it;
    std::list<std::pair<uint64_t, Vertex>>::iterator req_it;
    int v_round, sockfd;
    uint64_t v_peerID;
    char byte_stream[MAX_STREAM_SIZE];
    uint32_t true_size;
    // std::ofstream file;
    std::string filename = std::to_string(this->id);
    std::string filename_2 = filename + "_client";
    time_t start, last_record;
    double time_in_round;
    int stop = this->last_round;

    std::unordered_set<std::string> set;

    int requests_answered = 0;
    int missing_vertices = 0;
    bool need_help = false;
    int help_counter = 0;

    std::ofstream testfile;
    std::string pid_name = std::to_string(getpid()) + ".txt";
    // testfile.open(pid_name, std::ios::app);
    // testfile << "Started: " << getpid() << "\n";
    // testfile.close();
    std::mt19937 random_num_gen(this->rd());
    std::uniform_int_distribution<int> distribution(1, 100);

    std::time(&last_record);
    last_record = last_record - 30;

    while (true)
    {
        if (this->round <= this->last_round)
        {
            // Check if I can move to the next round
            if (this->round == 0 || (this->dag_image[this->round - 1].size() >= (size_t)(2 * this->byzantines + 1)))
            {
                need_help = false;

                if ((this->round % WAVE == 0) && (this->round > WAVE))
                {
                    dag_scan(this->round - WAVE, this->round - 2 * WAVE);
                }
                else if (this->round % WAVE == HALF_WAVE && this->round > 2 * WAVE)
                {
                    /* total order this part of the DAG */

                    int finish = this->round - (WAVE + HALF_WAVE);
                    total_order(finish, finish - WAVE);
                }

                /* Advance round and create a new vertex to broadcast */
                this->round++;

                if (this->round <= this->last_round)
                {
                    this->dag_image.emplace_back();
                    new_vertex =
                        Vertex(this->round, this->id, std::to_string(this->round) + "-" + std::to_string(this->id));
                    if (this->round > 1)
                        new_vertex.add_ref(this->dag_image[this->round - 2], this->id);
                    vertex_hash = new_vertex.hash();
                    this->server.add_to_map(vertex_hash, new_vertex);
                    this->dag_image[this->round - 1].emplace(vertex_hash);
                    // file.open(filename_2.c_str(), std::ios::app);
                    // file << new_vertex.get_round() << ":" << new_vertex.get_pid() << "\n";
                    // file.close();
                    true_size = new_vertex.serialize_data(byte_stream, sizeof(byte_stream));
                    attach_length_to_msg(byte_stream, true_size);
                    broadcast(byte_stream, true_size, this->round, this->id);
                    std::time(&start);
                }
            }
            else
            {
                time_in_round = std::difftime(std::time(NULL), start);
                if (time_in_round > (double)MAX_DELAY * 4)
                {
                    if (this->round == this->last_round)
                    {
                        stop--;
                    }
                    else
                    {
                        printf("ASKED FOR HELP : %ld ROUND %d\n", this->id, this->round);
                        dag_scan(this->round, this->round - WAVE);
                        std::time(&start);
                        need_help = true;
                        help_counter++;
                    }
                }
            }
        }

        this->u_lock.lock();
        this->server.set_thread_ready(true);

        // testfile.open(pid_name, std::ios::app);
        // testfile << getppid() << "\n";
        // testfile.close();

        this->cv.wait(this->u_lock);

        req_it = this->requests.begin();

        while (req_it != this->requests.end())
        {
            // std::thread(&Peer::deliver_request, this, *req_it).detach();
            deliver_request(*req_it);
            req_it = this->requests.erase(req_it);
            requests_answered++;
        }

        it = this->buffer.begin();

        while (it != this->buffer.end())
        {
            v_round = (*it).get_round();

            if (this->round >= v_round)
            {
                if (v_round == 1 || mutual_history(v_round, (*it).get_refs(), set, (*it).get_pid()))
                {
                    /* I can add this vertex to the DAG*/
                    if (!set.empty() && distribution(random_num_gen) <= 60)
                    {
                        missing_vertices++;
                        v_peerID = (*it).get_pid();
                        sockfd = this->client.get_socket(v_peerID);
                        // std::thread(&Peer::send_missing_vertices, this, sockfd, set).detach();
                        send_missing_vertices(sockfd, set);
                        set.clear();
                    }

                    this->dag_image[v_round - 1].emplace((*it).hash());
                    // file.open(filename.c_str(), std::ios::app);
                    // file << v_round << ":" << (*it).get_pid() << "\n";
                    // file.close();
                    it = this->buffer.erase(it);
                }
                else
                    it++;
            }
            else
                it++;
        }

        if (this->round > stop || (this->fail && this->round == 27))
        {
            this->server.is_up = false;
            this->server.set_thread_ready(true);
            this->u_lock.unlock();
            break;
        }

        // //file.open(std::to_string(this->id).c_str(), std::ios::app);
        // if (this->buffer.empty())
        // {
        //     file << "My buffer is empty!"
        //          << "\n";
        // }
        // else
        // {
        //     file << "My buffer contains " << this->buffer.size() << " elements!"
        //          << "\n";
        // }
        // file.close();

        if (std::time(NULL) - last_record >= 12)
        {
            write_record(&requests_answered, &missing_vertices, need_help, help_counter);
            std::time(&last_record);
        }
        this->u_lock.unlock();
    }
    // file.open(filename_2, std::ios::app);
    // file << "Client stopped running!!!\n";
    // file.close();
    this->client.close_sockets();
    printf("Client stopped running!!! %lu\n", this->id);
}

bool Peer::mutual_history(int v_round, const std::vector<std::string> &v_refs,
                          std::unordered_set<std::string> &temp_set, uint64_t pid)
{
    temp_set = this->dag_image[v_round - 2];

    for (std::string ref : v_refs)
    {
        if (this->dag_image[v_round - 2].find(ref) == this->dag_image[v_round - 2].end())
        {
            temp_set.clear();
            return false;
        }
        temp_set.erase(ref);
    }
    return true;
}

void Peer::start_thread()
{
    this->scan_buffer_thread = std::thread(&Peer::buffer_scan_move_round, this);
}

void Peer::join_threads()
{
    if (this->scan_buffer_thread.joinable())
    {
        this->scan_buffer_thread.join();
    }
    if (this->server_thread.joinable())
    {
        this->server_thread.join();
    }
}

uint64_t Peer::getid()
{
    return this->id;
}

void Peer::close_server()
{
    this->server.close_socket();
}

void Peer::send_missing_vertices(int peer_socket, std::unordered_set<std::string> &reference_set)
{
    std::unordered_set<std::string>::node_type extractor;
    Vertex vertex;
    char vertex_stream[MAX_STREAM_SIZE];
    char *packet_stream = nullptr;
    int stream_size;
    uint32_t packet_size = 0;
    int current_length = 0;
    int count = 0;
    char *heap_ptr;

    while (!reference_set.empty())
    {
        count++;
        extractor = reference_set.extract(reference_set.begin());
        this->server.find_in_map(extractor.value(), &vertex);

        stream_size = vertex.serialize_data(vertex_stream, sizeof(vertex_stream));
        packet_size += stream_size;
        packet_stream = (char *)realloc(packet_stream, packet_size * sizeof(char));
        if (packet_stream == nullptr)
        {
            printf("REALLOC PROBLEM!\n");
            exit(EXIT_FAILURE);
        }

        std::memcpy(packet_stream + current_length, vertex_stream, stream_size);
        current_length = packet_size;

        if (packet_size >= 40000)
        {
            /* send the vertices you have so far*/
            attach_length_to_msg(packet_stream, packet_size);
            printf("SENDING MISSING MESSAGES OF %d BYTES AND %d VERTICES!!!!\n", packet_size, count);
            heap_ptr = dynamic_copy(packet_stream, packet_size);
            Task task([peer_socket, heap_ptr, packet_size, this]()
                      { c_sendn(peer_socket, heap_ptr, packet_size, 0, this->rd); });
            this->pool.enqueue(task);
            free(packet_stream);
            packet_stream = nullptr;
            packet_size = 0;
            current_length = 0;
            count = 0;
        }
    }
    if (packet_stream != nullptr)
    {
        attach_length_to_msg(packet_stream, packet_size);
        printf("SENDING MISSING MESSAGES OF %d BYTES AND %d VERTICES!!!!\n", packet_size, count);
        heap_ptr = dynamic_copy(packet_stream, packet_size);
        Task task([peer_socket, heap_ptr, packet_size, this]()
                  { c_sendn(peer_socket, heap_ptr, packet_size, 0, this->rd); });
        this->pool.enqueue(task);
        free(packet_stream);
    }
}

void Peer::dag_scan(int end, int start)
{
    if (start < 0)
        start = 0;

    for (int i = start; i < end; i++)
    {
        if (this->dag_image[i].size() < this->crowd)
        {
            // check_round(i);
            std::thread(&Peer::check_round, this, i).detach();
        }
    }
}

void Peer::check_round(int round)
{
    std::string hash;
    uint64_t peerID;
    int socket;

    /* We are missing one or more vertices from this round */
    // #pragma omp paraller for
    for (int j = 0; j < this->crowd - 1; j++)
    {
        peerID = this->client.get_peer_id_at(j);
        if (!black_list.find(peerID))
        {
            hash_256(std::to_string(round + 1) + std::to_string(peerID), hash);

            if (!this->server.find_in_map(hash, nullptr))
            {
                /* This hash is missing and I need to make a request for it */

                socket = this->client.get_socket(peerID);
                // std::thread(&Peer::make_request, this, hash, socket).detach();
                // Task task([hash, socket, this]()
                //           { make_request(hash, socket); });
                // this->req_pool.enqueue(task);
                make_request(hash, socket);
            }
        }
    }
}

void Peer::make_request(std::string hash, int owner)
{
    char buffer[75];
    std::vector<int> sockets;
    std::vector<char *> heap_ptrs(this->byzantines + 2);
    char *heap_ptr;
    int sockfd;
    bool sent_to_owner = false;
    uint64_t id = htobe64(this->id);
    size_t len = hash.size() + sizeof(uint64_t) + 1;
    this->requests_asked++;

    sockets = this->client.c_sockets;

    std::memset(buffer, 0, sizeof(buffer));
    buffer[0] = PREFIX;
    std::memcpy(buffer + 1, hash.c_str(), hash.size());
    std::memcpy(buffer + hash.size() + 1, &id, sizeof(id));

    for (int i = 0; i < heap_ptrs.size(); i++)
    {
        heap_ptrs[i] = dynamic_copy(buffer, len);
    }

    uint64_t seed = std::chrono::system_clock::now().time_since_epoch().count();
    std::default_random_engine rng(seed);

    std::shuffle(sockets.begin(), sockets.end(), rng);

    for (int i = 0; i < this->byzantines + 1; i++)
    {
        sockfd = sockets[i];
        // heap_ptr = dynamic_copy(buffer, len);
        Task task([i, sockfd, heap_ptrs, len, this]()
                  { c_sendn(sockfd, heap_ptrs[i], len, 0, this->rd); });
        this->pool.enqueue(task);
        if (sockets[i] == owner)
            sent_to_owner = true;
    }

    if (!sent_to_owner)
    {
        // heap_ptr = dynamic_copy(buffer, len);
        Task task([owner, heap_ptrs, len, this]()
                  { c_sendn(owner, heap_ptrs[heap_ptrs.size() - 1], len, 0, this->rd); });
        this->pool.enqueue(task);
    }
    else
        free(heap_ptrs[heap_ptrs.size() - 1]);
}

void Peer::deliver_request(std::pair<uint64_t, Vertex> request)
{
    char buffer[MAX_STREAM_SIZE];
    char *heap_ptr;
    int sockfd = this->client.get_socket(request.first);
    uint32_t msg_length = request.second.serialize_data(buffer, sizeof(buffer));
    attach_length_to_msg(buffer, msg_length);
    heap_ptr = dynamic_copy(buffer, msg_length);
    Task task([sockfd, heap_ptr, msg_length, this]()
              { c_sendn(sockfd, heap_ptr, msg_length, 0, this->rd); });
    this->pool.enqueue(task);
}

void Peer::total_order(int end, int start)
{
    std::string hash;
    uint64_t peerID;

    for (int i = start; i < end; i++)
    {
        if (dag_image[i].size() < this->crowd)
        {
            /* We found a missing vertex */
            for (int j = 0; j < this->crowd - 1; j++)
            {
                peerID = this->client.get_peer_id_at(j);
                hash_256(std::to_string(i + 1) + std::to_string(peerID), hash);

                if (dag_image[i].find(hash) == dag_image[i].end())
                {
                    /* This hash is missing and most propably its creator has somehow failed  */
                    this->black_list.emplace(peerID);
                    int dead_socket = this->client.socket_map[peerID];
                    for (auto it = this->client.c_sockets.begin(); it != this->client.c_sockets.end(); it++)
                    {
                        if ((*it) == dead_socket)
                        {
                            it = this->client.c_sockets.erase(it);
                        }
                    }
                }
            }
        }
    }
}

void Peer::hash_set(int index, std::string &output)
{
    std::ofstream file;
    std::string filename = "peer_" + std::to_string(getpid());

    std::vector<std::string> vector;
    std::stringstream str;
    vector.resize(this->dag_image[index].size());
    // file.open(filename, std::ios::app);
    // file << "SET SIZE: " << vector.size() << "\n";
    // file.close();

    std::copy(this->dag_image[index].begin(), this->dag_image[index].end(), vector.begin());
    std::sort(vector.begin(), vector.end());
    // file.open(filename, std::ios::app);
    // file << "SET SIZE: " << vector.size() << "\n";
    // file.close();

    for (int i = 0; i < vector.size(); i++)
    {
        str << vector[i];
        // file.open(filename, std::ios::app);
        //  file << vector[i] << "\n";
        //  file.close();
    }
    hash_256(str.str(), output);
}

void Peer::hash_dag(std::string &output)
{
    std::stringstream str;

    for (int i = 0; i < this->dag_image.size() - 3 * WAVE; i++)
    {
        hash_set(i, output);
        str << output;
        output.clear();
    }

    hash_256(str.str(), output);
    // auto iterator = this->dag_image[0].begin();
    // std::string out;
    // hash_256((*iterator), out);
    // std::cout << out << std::endl;

    // hash_set(0, out);
    // std::cout << out << std::endl;
}

void Peer::write_record(int *requests_answered, int *missing_vertices, bool help, int times_i_need_help)
{
    std::ofstream file;
    int total_absent_vertices = 0;
    int absent_vertices = 0;
    int total_vertices = 0;
    int request_multicasts;
    int vertex_broadcasts;
    int total_messages;
    std::string filename = "records/" + std::to_string(this->id) + "_records.csv";
    std::chrono::time_point point = std::chrono::system_clock::now();
    std::chrono::milliseconds mills = std::chrono::duration_cast<std::chrono::milliseconds>(point.time_since_epoch());
    std::time_t date = std::chrono::system_clock::to_time_t(point);

    std::stringstream ss;
    ss << std::put_time(std::localtime(&date), "%d-%m-%Y %H:%M:%S:") << std::setfill('0') << std::setw(3)
       << mills.count() % 1000;

    for (int i = 0; i < this->round; i++)
    {
        total_vertices += dag_image[i].size();
        if (dag_image[i].size() < this->crowd)
        {
            absent_vertices = this->crowd - dag_image[i].size();
            total_absent_vertices += absent_vertices;
        }
    }

    vertex_broadcasts = this->round * (crowd - 1);
    request_multicasts = this->requests_asked * (2 * this->byzantines + 1);
    total_messages = vertex_broadcasts + request_multicasts + *missing_vertices + *requests_answered;

    // file.open(filename, std::ios::app);
    // file << ss.str() << "," << this->round << "," << this->dag_image[round - 1].size() << "," << total_vertices << ","
    //      << total_absent_vertices << "," << times_i_need_help << "," << std::boolalpha << help << ","
    //      << *requests_answered << "," << *missing_vertices << "," << vertex_broadcasts << "," << request_multicasts
    //      << "," << vertex_broadcasts + request_multicasts + *missing_vertices + *requests_answered << ","
    //      << black_list.size() << "\n";
    // file.close();

    file.open(filename, std::ios::app);
    file << ss.str() << "," << this->round << "," << total_vertices << "," << total_absent_vertices << "," << total_messages << ","
         << times_i_need_help << "," << this->dag_image[round - 1].size() << "," << std::boolalpha << help << ","
         << *requests_answered << "," << *missing_vertices << "," << vertex_broadcasts << "," << request_multicasts << "," << black_list.size() << "\n";
    file.close();
}

void Peer::broadcast(char *data, size_t size, int round, uint64_t myid)
{
    std::vector<char *> heap_ptrs(this->crowd - 1);

    int count = 0;
    for (int socket_fd : this->client.c_sockets)
    {
        heap_ptrs[count] = dynamic_copy(data, size);

        Task task([socket_fd, count, heap_ptrs, size, this]()
                  { c_sendn(socket_fd, heap_ptrs[count], size, 0, this->rd); });
        this->pool.enqueue(task);
        printf("SEND %ld DATA ON ROUND %d : %lu\n", size, round, myid);
        count++;
    }
}
