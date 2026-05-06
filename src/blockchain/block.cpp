#include "midnight/blockchain/block.hpp"
#include <openssl/sha.h>
#include <cstring>
#include <sstream>
#include <iomanip>

namespace midnight::blockchain
{

    Block::Block(uint64_t height, const std::string &prev_hash)
    {
        header_.height = height;
        header_.prev_hash = prev_hash;
        header_.version = 1;
        header_.timestamp = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        header_.nonce = 0;
        header_.difficulty = 0;
        header_.merkle_root = "";
    }

    std::string Block::calculate_hash() const
    {
        // Proper SHA256 hash of the serialized block header
        std::ostringstream ss;
        ss << std::hex << std::setfill('0');

        // version (8 bytes LE)
        uint64_t ver = header_.version;
        for (int i = 0; i < 8; ++i)
            ss << std::setw(2) << ((ver >> (i * 8)) & 0xFF);

        // height (8 bytes LE)
        uint64_t h = header_.height;
        for (int i = 0; i < 8; ++i)
            ss << std::setw(2) << ((h >> (i * 8)) & 0xFF);

        // prev_hash (variable length, hex string)
        for (size_t i = 0; i < header_.prev_hash.size(); ++i) {
            char c = header_.prev_hash[i];
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
            ss << std::setw(2) << (int)(static_cast<uint8_t>(c));
        }

        // merkle_root (variable length, hex string)
        for (size_t i = 0; i < header_.merkle_root.size(); ++i) {
            char c = header_.merkle_root[i];
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
            ss << std::setw(2) << (int)(static_cast<uint8_t>(c));
        }

        // timestamp (8 bytes LE)
        uint64_t ts = header_.timestamp;
        for (int i = 0; i < 8; ++i)
            ss << std::setw(2) << ((ts >> (i * 8)) & 0xFF);

        // nonce (8 bytes LE)
        uint64_t n = header_.nonce;
        for (int i = 0; i < 8; ++i)
            ss << std::setw(2) << ((n >> (i * 8)) & 0xFF);

        // difficulty (4 bytes LE)
        uint32_t d = header_.difficulty;
        for (int i = 0; i < 4; ++i)
            ss << std::setw(2) << ((d >> (i * 8)) & 0xFF);

        // Transactions merkle root (if any)
        if (!transactions_.empty()) {
            for (const auto &tx : transactions_) {
                for (size_t i = 0; i < tx.size(); ++i) {
                    char c = tx[i];
                    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
                    ss << std::setw(2) << (int)(static_cast<uint8_t>(c));
                }
            }
        }

        std::string preimage = ss.str();

        // SHA256 hash
        uint8_t hash_out[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const uint8_t*>(preimage.data()), preimage.size(), hash_out);

        std::ostringstream out;
        out << std::hex << std::setfill('0');
        for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
            out << std::setw(2) << static_cast<int>(hash_out[i]);

        return out.str();
    }

    void Block::add_transaction(const std::string &tx_hash)
    {
        transactions_.push_back(tx_hash);
    }

} // namespace midnight::blockchain
