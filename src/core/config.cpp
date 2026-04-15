#include "midnight/core/config.hpp"
#include <fstream>
#include <sstream>

namespace midnight
{

    std::shared_ptr<Config> g_config = std::make_shared<Config>();

    void Config::load(const std::string &config_path)
    {
        std::ifstream file(config_path);
        if (!file.is_open())
        {
            return;
        }

        std::string line;
        while (std::getline(file, line))
        {
            if (line.empty() || line[0] == '#')
                continue;

            size_t delimiter_pos = line.find('=');
            if (delimiter_pos != std::string::npos)
            {
                std::string key = line.substr(0, delimiter_pos);
                std::string value = line.substr(delimiter_pos + 1);
                config_map_[key] = value;
            }
        }
    }

    void Config::set(const std::string &key, const std::string &value)
    {
        config_map_[key] = value;
    }

    std::string Config::get(const std::string &key, const std::string &default_value) const
    {
        auto it = config_map_.find(key);
        return it != config_map_.end() ? it->second : default_value;
    }

    const std::map<std::string, std::string> &Config::get_all() const
    {
        return config_map_;
    }

    bool Config::has(const std::string &key) const
    {
        return config_map_.find(key) != config_map_.end();
    }

} // namespace midnight
