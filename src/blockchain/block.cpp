#include "midnight/blockchain/block.hpp"
#include <sstream>
#include <iomanip>

namespace midnight::blockchain
{

    Block::Block(uint64_t height, const std::string &prev_hash)
    {
        header_.height = height;
        header_.prev_hash = prev_hash;
        header_.version = 1;
        header_.timestamp = 0;
        header_.nonce = 0;
        header_.difficulty = 0;
    }

    std::string Block::calculate_hash() const
    {
        // Simplified hash calculation - would use SHA256 in production
        std::stringstream ss;
        ss << std::hex << header_.height << header_.timestamp << header_.nonce;
        return ss.str();
    }

    void Block::add_transaction(const std::string &tx_hash)
    {
        transactions_.push_back(tx_hash);
    }

} // namespace midnight::blockchain
