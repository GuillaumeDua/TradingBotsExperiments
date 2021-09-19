#pragma once

#include <tuple>

namespace trading_bots::details::mp {
    // todo : universal template parameter, to merge ttps/nttps

    template <typename... Ts>
    struct are_unique_ttps {
        constexpr static bool value = []<typename first, typename ... rest>(){
            return (not(std::is_same_v<first, rest> or ...)) and are_unique_ttps<rest...>::value;
        }.template operator()<Ts...>();
    };
    template <>
    struct are_unique_ttps<> {
        constexpr static bool value = true;
    };
    template <typename... Ts>
    constexpr auto are_unique_ttps_v = are_unique_ttps<Ts...>::value;

    template <auto first, auto... values>
    struct are_unique_nttps {
        constexpr static bool value = (not((first == values) or ...)) and are_unique_nttps<values...>::value;
    };
    template <auto arg>
    struct are_unique_nttps<arg> {
        constexpr static bool value = true;
    };
    template <auto... values>
    constexpr auto are_unique_nttps_v = are_unique_nttps<values...>::value;

    template <typename T>
    struct unique_ttps {
        static_assert([](){ return false; }(), "unique_ttps : not a template-template parameter");
    };
    template <template <typename ...> typename T, typename ... Ts>
    struct unique_ttps<T<Ts...>> {
        constexpr static bool value = are_unique_ttps_v<Ts...>;
    };
    template <typename T>
    constexpr bool has_unique_ttps_v = unique_ttps<T>::value;

    template <typename T>
    struct unique_nttps {
        static_assert([](){ return false; }(), "unique_ttps : not a template-template parameter");
    };
    template <template <auto ...> typename T, auto ... values>
    struct unique_nttps<T<values...>> {
        constexpr static bool value = are_unique_nttps_v<values...>;
    };
    template <typename T>
    constexpr bool has_unique_nttps_v = unique_nttps<T>::value;

    template <typename ...>
    struct pack_type{};
}
namespace trading_bots::details {
    template <typename T>
    concept TupleType = requires { std::tuple_size_v<std::remove_reference_t<T>>; };

    template <typename T>
    concept StorageType = TupleType<T> and mp::has_unique_ttps_v<T>;
}