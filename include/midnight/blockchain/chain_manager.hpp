#pragma once

#include <string>
#include <vector>
#include <memory>

namespace midnight::blockchain
{

    class Block;
    class Transaction;

    /**
     * @brief Blockchain chain manager
     */
    class ChainManager
    {
    public:
        /**
         * @brief Initialize blockchain
         */
        void initialize();

        /**
         * @brief Add block to chain
         */
        bool add_block(std::shared_ptr<Block> block);

        /**
         * @brief Get block by height
         */
        std::shared_ptr<Block> get_block(uint64_t height) const;

        /**
         * @brief Get block by hash
         */
        std::shared_ptr<Block> get_block_by_hash(const std::string &hash) const;

        /**
         * @brief Get chain height
         */
        uint64_t get_height() const { return chain_height_; }

        /**
         * @brief Validate block
         */
        bool validate_block(const Block &block) const;

        /**
         * @brief Get latest block
         */
        std::shared_ptr<Block> get_latest_block() const;

    private:
        std::vector<std::shared_ptr<Block>> blockchain_;
        uint64_t chain_height_ = 0;
    };

} // namespace midnight::blockchain
