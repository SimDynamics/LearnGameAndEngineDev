#pragma once
#include "core/lesson_state.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace lgaed::attempts {
inline constexpr std::int64_t recovery_millis=60LL*60*1000;
// Personal Windows build: change this constant in source to enable Premium.
// There is no purchase, network entitlement or UI activation path.
inline constexpr bool personal_premium_enabled=true;
inline bool premium(const state::StateData& data){
    return personal_premium_enabled||state::string_value(data,"premium_state","state")=="PREMIUM";
}
inline int maximum(std::string_view route){return route=="math_physics"?12:8;}
inline double number(const state::StateData& data,std::string_view key,double fallback){
    const auto value=state::find(data,"attempt_reserve_seconds_test",key);
    if(!value)return fallback;
    if(const auto decimal=std::get_if<double>(value))return std::isfinite(*decimal)?*decimal:fallback;
    if(const auto integer=std::get_if<std::int64_t>(value))return static_cast<double>(*integer);
    return fallback;
}
// Preserves Android's one-time conversion of the old one-minute test balance.
// No balance is created until a deduction; the original reserve stays shared.
inline void initialize(state::StateData& data,std::string_view route,std::int64_t now){
    const auto suffix=std::string(route),interval="recovery_interval_"+suffix;
    if(state::integer_value(data,"attempt_reserve_seconds_test",interval)==recovery_millis)return;
    auto& preferences=data.preferences["attempt_reserve_seconds_test"];
    const auto reserve="reserve_"+suffix,updated="updated_at_"+suffix;
    if(preferences.contains(reserve)&&preferences.contains(updated)){
        const auto previous=std::max<std::int64_t>(1,state::integer_value(data,"attempt_reserve_seconds_test",interval,60000));
        const auto elapsed=std::max<std::int64_t>(0,now-state::integer_value(data,"attempt_reserve_seconds_test",updated,now));
        preferences[reserve]=std::clamp(number(data,reserve,maximum(route))+static_cast<double>(elapsed)/previous,0.,static_cast<double>(maximum(route)));
        preferences[updated]=now;
    }
    preferences[interval]=recovery_millis;
}
struct Balance {
    int available=0,maximum=0;
    double reserve=0,progress=0;
    bool unlimited=false;
    [[nodiscard]] bool can_evaluate()const{return unlimited||available>0;}
};
// Original AttemptRecoveryLabel: time until the whole reserve is refilled.
inline std::string recovery_label(int limit,double reserve,bool unlimited){
    if(unlimited||limit<=0||!std::isfinite(reserve))return {};
    const auto missing=std::clamp(limit-reserve,0.,static_cast<double>(limit));if(missing<=0)return {};
    const auto minutes=std::max<std::int64_t>(1,static_cast<std::int64_t>(std::ceil(missing*recovery_millis/60000.-.0001)));
    const auto hours=minutes/60,remainder=minutes%60;
    if(hours==0)return std::to_string(minutes)+"m";
    if(remainder==0)return std::to_string(hours)+"h";
    return std::to_string(hours)+"h "+std::to_string(remainder)+"m";
}
inline Balance balance(const state::StateData& data,std::string_view route,std::int64_t now){
    const auto limit=maximum(route);const auto suffix=std::string(route);
    const auto saved=std::clamp(number(data,"reserve_"+suffix,limit),0.,static_cast<double>(limit));
    const auto updated=state::integer_value(data,"attempt_reserve_seconds_test","updated_at_"+suffix,now);
    const auto elapsed=std::max<std::int64_t>(0,now-updated);
    const auto reserve=std::clamp(saved+static_cast<double>(elapsed)/recovery_millis,0.,static_cast<double>(limit));
    return {static_cast<int>(reserve),limit,reserve,reserve/limit,premium(data)};
}
inline Balance consume_incorrect(state::StateData& data,std::string_view route,std::int64_t now){
    initialize(data,route,now);const auto current=balance(data,route,now);
    if(current.unlimited||current.available<=0)return current;
    auto& preferences=data.preferences["attempt_reserve_seconds_test"];
    preferences["reserve_"+std::string(route)]=std::max(0.,current.reserve-1.);
    preferences["updated_at_"+std::string(route)]=now;
    return balance(data,route,now);
}
}
