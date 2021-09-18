#pragma once

#include <trading_bots/details/coroutine.hpp>
#include <trading_bots/data_types.hpp>

#include <fstream>
#include <string>
#include <stack>

#ifndef fwd
# define fwd(...) static_cast<decltype(__VA_ARGS__) &&>(__VA_ARGS__)
#endif

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
    auto make_record(std::string && line) {

        auto value = trading_bots::data_types::record {
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
namespace trading_bots::details::io::csv::file {
    auto generate_ifstream(const std::string & path) {
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

    trading_bots::details::coro::generator<trading_bots::data_types::record> record_generator_factory(std::ifstream & inputs){

        std::string buffer;
        while (true) {
            if (not std::getline(inputs, buffer))
                co_return;
            else co_yield trading_bots::details::io::csv::make_record(std::move(buffer));
        }
    }

    auto generate_datas(auto && path) {
        auto ifs = generate_ifstream(path);
        auto record_generator = record_generator_factory(ifs);

        std::stack<trading_bots::data_types::record> records;
        while (record_generator.next())
            records.push(record_generator.getValue());
        return records;
    }
}

#undef fwd