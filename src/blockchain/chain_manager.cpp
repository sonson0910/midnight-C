#include "midnight/blockchain/chain_manager.hpp"
#include "midnight/blockchain/block.hpp"

namespace midnight::blockchain
{

    void ChainManager::initialize()
    {
        // Create genesis block
        auto genesis = std::make_shared<Block>(0, "0");
        blockchain_.push_back(genesis);
        chain_height_ = 1;
    }

    bool ChainManager::add_block(std::shared_ptr<Block> block)
    {
        if (!validate_block(*block))
        {
            return false;
        }

        blockchain_.push_back(block);
        chain_height_++;
        return true;
    }

    std::shared_ptr<Block> ChainManager::get_block(uint64_t height) const
    {
        if (height < blockchain_.size())
        {
            return blockchain_[height];
        }
        return nullptr;
    }

    std::shared_ptr<Block> ChainManager::get_block_by_hash(const std::string &hash) const
    {
        for (const auto &block : blockchain_)
        {
            if (block->calculate_hash() == hash)
            {
                return block;
            }
        }
        return nullptr;
    }

    bool ChainManager::validate_block(const Block &block) const
    {
        // Simplified validation - would include more checks in production
        return true;
    }

    std::shared_ptr<Block> ChainManager::get_latest_block() const
    {
        if (!blockchain_.empty())
        {
            return blockchain_.back();
        }
        return nullptr;
    }

} // namespace midnight::blockchain
