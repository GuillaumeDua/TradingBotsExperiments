#pragma once

#include <trading_bots/details/coroutine.hpp>
// #include <trading_bots/business/data_types.hpp>

#include <fstream>
#include <string>
#include <stack>
#include <coroutine>

#ifndef fwd
# define fwd(...) static_cast<decltype(__VA_ARGS__) &&>(__VA_ARGS__)
#endif

namespace trading_bots::details::io::concepts {

    template <typename T>
    concept io_record_type = requires {
        T {
            .Low = std::declval<float>(),
            .High = std::declval<float>(),
            .Open = std::declval<float>(),
            //, Volume(std::stoi(csv::extract_last_field(fwd(line))))
            .Volume = std::declval<std::string>(),
            .CloseLast = std::declval<float>(),
            .Date = std::declval<std::string>(),
        };
    };
}

namespace trading_bots::details::io::csv {
    auto extract_last_field(std::string && line) {
        if (auto pos = line.rfind(','); pos not_eq std::string::npos) {
            auto value = line.substr(pos + 1);
            line.resize(pos);
            return value;
        }
        else if (not line.empty()) {
            return std::move(fwd(line));
        }
        else
            throw std::invalid_argument{"incomplete input"};
    }
    
    namespace concepts = trading_bots::details::io::concepts;
    template <concepts::io_record_type record_type>
    auto make_record(std::string && line) {

        auto value = record_type {
            .Low = std::stof(csv::extract_last_field(fwd(line))),
            .High = std::stof(csv::extract_last_field(fwd(line))),
            .Open = std::stof(csv::extract_last_field(fwd(line))),
            //, Volume(std::stoi(csv::extract_last_field(fwd(line))))
            .Volume = csv::extract_last_field(fwd(line)),
            .CloseLast = std::stof(csv::extract_last_field(fwd(line))),
            .Date = csv::extract_last_field(fwd(line))
        };
        value.ensure_datas_integrity();
        return value;
    }
}

namespace trading_bots::details::io::csv {

    namespace concepts = trading_bots::details::io::concepts;
    template <concepts::io_record_type record_type>
    struct file {

        file(const std::string & path)
        : ifs{ generate_ifstream(path) }
        {
            if (not ifs.is_open())
                throw std::invalid_argument{"trading_bots::details::io::csv::file : cannot open"};
        }

        auto extract_datas() {
            auto record_generator = records_extractor_factory();

            std::stack<record_type> records;
            while (record_generator.next())
                records.push(record_generator.getValue());
            return records;
        }

    private:

        std::ifstream ifs;

        static auto generate_ifstream(const std::string & path) {
            using namespace std::literals;
            constexpr auto file_header = "Date,Close/Last,Volume,Open,High,Low"sv;

            std::ifstream ifs{ path };
            if (std::string line_buffer; not std::getline(ifs, line_buffer)) {
                throw std::runtime_error{"empty file"};
            }
            else if (line_buffer not_eq file_header) {
                throw std::runtime_error{"bad header"};
            }
            return ifs;
        }
        trading_bots::details::coro::generator<record_type> records_extractor_factory(){

            std::string buffer;
            while (true) {
                if (not std::getline(ifs, buffer))
                    co_return;
                else co_yield trading_bots::details::io::csv::make_record<record_type>(std::move(buffer));
            }
        }
    };
}

#undef fwd