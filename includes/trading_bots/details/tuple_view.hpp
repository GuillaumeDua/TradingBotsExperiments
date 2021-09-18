#pragma once

#include <trading_bots/details/mp.hpp>

namespace trading_bots::details::tuple_view {
    // poc : https://godbolt.org/z/evod9Eo9o

    template <typename ... Ts>
    requires details::mp::are_unique_ttps_v<Ts...>
    using tuple_view_type = std::tuple<std::add_lvalue_reference_t<Ts>...>;
    template <typename ... Ts>
    requires details::mp::are_unique_ttps_v<Ts...>
    using tuple_const_view_type = std::tuple<std::add_lvalue_reference_t<std::add_const_t<Ts>>...>;

    template <typename ... components_ts>
    constexpr auto make_tuple_view(details::StorageType auto & storage)
    requires requires { ((std::get<components_ts>(storage)), ...); }
    {
        using result_type = tuple_view_type<components_ts...>;
        return result_type{ std::get<components_ts>(storage)... };
    }
    template <typename ... components_ts>
    constexpr auto make_tuple_view(const details::StorageType auto & storage)
    requires requires { ((std::get<components_ts>(storage)), ...); }
    {
        using result_type = tuple_const_view_type<components_ts...>;
        return tuple_const_view_type{ std::get<components_ts>(storage)... };
    }
}