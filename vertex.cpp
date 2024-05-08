#include "vertex.h"

// Vertex constructor

Vertex::Vertex(int round, uint64_t peer_id, std::string payload) : round(round), peer_id(peer_id), payload(payload) {}

// Vertex move constructor

Vertex::Vertex(Vertex &&source) noexcept
{
    round = source.round;
    peer_id = source.peer_id;
    payload = std::move(source.payload);
    references = std::move(source.references);

    source.round = 0;
    source.peer_id = 0;
    source.payload.clear();
    source.references.clear();
}

// Vertex move assignment operator

Vertex &Vertex::operator=(Vertex &&source) noexcept
{
    if (this != &source)
    {
        this->payload.clear();
        this->references.clear();

        round = source.round;
        peer_id = source.peer_id;
        payload = std::move(source.payload);
        references = std::move(source.references);

        source.round = 0;
        source.peer_id = 0;
        source.payload.clear();
        source.references.clear();
    }
    return *this;
}

// Vertex methods

bool Vertex::add_ref(std::unordered_set<std::string> &set, uint64_t peer_id)
{
    if (peer_id == this->peer_id)
    {
        this->references.insert(this->references.end(), set.begin(), set.end());
        return true;
    }
    return false;
}

std::string Vertex::hash() const
{
    std::string hashed_vertex;
    hash_256(std::to_string(this->round) + std::to_string(this->peer_id), hashed_vertex);
    return hashed_vertex;
}

std::string Vertex::get_payload() const
{
    return this->payload;
}

std::vector<std::string> Vertex::get_refs() const
{
    return this->references;
}

int Vertex::get_round() const
{
    return this->round;
}

uint64_t Vertex::get_pid()
{
    return this->peer_id;
}

std::string Vertex::print()
{

    std::string str = std::to_string(this->round) + " - " + std::to_string(this->peer_id);
    return str;
}

// int round;
// uint64_t peer_id;
// std::string payload;
// std::vector<const std::string> references;

uint32_t Vertex::serialize_data(char *buffer, size_t size) const
{
    // std::string delim = "f";

    uint32_t data_size = sizeof(data_size);
    // clear the buffer
    std::memset(buffer, 0, size);

    // round processing
    int round = htonl(this->round);
    std::memcpy(buffer + data_size, &round, sizeof(round));
    data_size += sizeof(round);

    // peer_id processing
    uint64_t p_id = htobe64(this->peer_id);
    std::memcpy(buffer + data_size, &p_id, sizeof(p_id));
    data_size += sizeof(p_id);

    // payload processing
    int payload_length = htonl(this->payload.size());
    std::memcpy(buffer + data_size, &payload_length, sizeof(payload_length));
    data_size += sizeof(payload_length);

    std::memcpy(buffer + data_size, this->payload.c_str(), this->payload.size());
    data_size += this->payload.size();

    // vector processing
    int vector_size = htonl(this->references.size());
    std::memcpy(buffer + data_size, &vector_size, sizeof(vector_size));
    data_size += sizeof(vector_size);

    if (this->references.size() > 0)
    {
        int ref_length = htonl(this->references[0].size()); // Due to the sha256, all the references are of the same size
        std::memcpy(buffer + data_size, &ref_length, sizeof(ref_length));
        data_size += sizeof(ref_length);

        int counter = 0;
        for (std::string ref : this->references)
        {
            std::memcpy(buffer + data_size, ref.c_str(), ref.size());
            if (buffer[data_size + 50] == '\0')
            {
                printf("MEMCPY FAILED : %d %ld %ld\n", counter, ref.size(), this->references.size());
            }

            data_size += ref.size();
            counter++;
        }
    }

    for (int i = 0; i < 4; i++)
    {
        buffer[data_size] = DELIM;
        data_size += 1;
    }

    // buffer[data_size] = DELIM;
    // data_size += 1;

    return data_size;
}

void Vertex::deserialize_data(char *buffer, size_t size)
{
    this->round = 0;
    this->peer_id = 0;
    this->payload.clear();
    this->references.clear();

    uint32_t data = sizeof(data);

    // round fetch
    int round;
    std::memcpy(&round, buffer + data, sizeof(round));
    this->round = ntohl(round);
    data += sizeof(round);

    // peer_id fetch
    uint64_t p_id;
    std::memcpy(&p_id, buffer + data, sizeof(p_id));
    this->peer_id = be64toh(p_id);
    data += sizeof(peer_id);

    // payload fetch
    int payload_length;
    std::memcpy(&payload_length, buffer + data, sizeof(payload_length));

    data += sizeof(payload_length);
    // printf("PAYLOAD LENGTH  %d ROUND %d\n", ntohl(payload_length), this->round);
    this->payload.assign(buffer + data, ntohl(payload_length));
    data += this->payload.size();

    // vector fetch
    int vector_size, ref_length;
    std::memcpy(&vector_size, buffer + data, sizeof(vector_size));
    data += sizeof(vector_size);

    if (ntohl(vector_size) > 0)
    {
        std::memcpy(&ref_length, buffer + data, sizeof(ref_length));
        data += sizeof(ref_length);

        for (int i = 0; i < ntohl(vector_size); i++)
        {
            this->references.emplace_back(buffer + data, ntohl(ref_length));
            data += this->references[i].size();
        }
    }
}

// Other functions

void hash_256(const std::string input, std::string &output)
{
    CryptoPP::SHA256 hash;
    CryptoPP::byte digest[CryptoPP::SHA256::DIGESTSIZE];
    hash.Update((CryptoPP::byte *)input.data(), input.size());
    hash.Final(digest);

    CryptoPP::HexEncoder encoder;
    encoder.Put(digest, sizeof(digest));
    encoder.MessageEnd();

    CryptoPP::word64 size = encoder.MaxRetrievable();
    output.resize(size);
    encoder.Get((CryptoPP::byte *)&output[0], output.size());
}