#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace midnight::blockchain
{

    /**
     * @brief Represents a blockchain block
     */
    class Block
    {
    public:
        struct Header
        {
            uint64_t version;
            uint64_t height;
            std::string prev_hash;
            std::string merkle_root;
            uint64_t timestamp;
            uint64_t nonce;
            uint32_t difficulty;
        };

        Block(uint64_t height, const std::string &prev_hash);

        /**
         * @brief Calculate block hash
         */
        std::string calculate_hash() const;

        /**
         * @brief Get block header
         */
        const Header &get_header() const { return header_; }

        /**
         * @brief Add transaction hash to block
         */
        void add_transaction(const std::string &tx_hash);

        /**
         * @brief Get all transactions
         */
        const std::vector<std::string> &get_transactions() const { return transactions_; }

    private:
        Header header_;
        std::vector<std::string> transactions_;
    };

} // namespace midnight::blockchain
