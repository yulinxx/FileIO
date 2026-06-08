#pragma once

#include <string>
#include <vector>

namespace Fio
{
    struct ParseResult
    {
        bool success = false;
        std::string errorMessage;
        std::vector<std::string> warnings;

        static ParseResult ok()
        {
            return { true, {}, {} };
        }

        static ParseResult fail(const std::string& msg)
        {
            return { false, msg, {} };
        }

        static ParseResult fail(const std::string& msg, const std::vector<std::string>& warns)
        {
            return { false, msg, warns };
        }
    };

    struct WriteResult
    {
        bool success = false;
        std::string errorMessage;

        static WriteResult ok()
        {
            return { true, {} };
        }
        static WriteResult fail(const std::string& msg)
        {
            return { false, msg };
        }
    };
} // namespace Fio
