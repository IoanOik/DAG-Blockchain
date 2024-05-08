#include <iostream>
#include <vector>
#include <unordered_set>
#include <arpa/inet.h>
#include <cryptopp/sha.h>
#include <cryptopp/hex.h>
#define HEX_HASH 64
#define DELIM '#'
#define PREFIX '?'

class Vertex
{
private:
    int round;
    uint64_t peer_id;
    std::string payload;
    std::vector<std::string> references;

public:
    Vertex() = default;
    Vertex(const Vertex &source) = default;
    Vertex &operator=(Vertex &source) = default;
    Vertex(int round, uint64_t peer_id, std::string payload);
    Vertex(Vertex &&source) noexcept;
    Vertex &operator=(Vertex &&source) noexcept;

    bool add_ref(std::unordered_set<std::string> &set, uint64_t peer_id);
    std::string get_payload() const;
    std::vector<std::string> get_refs() const;
    std::string hash() const;
    uint32_t serialize_data(char *buffer, size_t size) const;
    void deserialize_data(char *buffer, size_t size);
    std::string print();
    int get_round() const;
    uint64_t get_pid();
};

void hash_256(const std::string input, std::string &output);