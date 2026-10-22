#include "core/scientific_calculator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <utility>

int main() {
    using namespace lgaed::math;
    try {
        const auto close = [](double actual,double expected) { if (std::abs(actual - expected) > 1e-11 * std::max(1.0,std::abs(expected))) throw std::runtime_error("Incorrect scientific result"); };
        close(calculate("2+3*4"),14); close(calculate("(2+3)*4"),20);
        close(calculate("-2^2"),-4); close(calculate("2^-3"),.125); close(calculate("2^3^2"),512);
        close(calculate("3!+4!"),30); close(calculate("0!"),1);
        // Original Android percentagesAreAlwaysPostfixDivisionByOneHundred.
        // Its implicit-multiplication example is written with the explicit '*'
        // required by this existing native calculator, without broadening it.
        for (const auto& [expression,expected] : std::array<std::pair<const char*,double>,10>{{
                {"50%",.5}, {"12.5%",.125}, {"200*10%",20.0}, {"50%+25%",.75},
                {"200+10%",200.1}, {"-50%",-.5}, {"(20+30)%",.5}, {"50%%",.005},
                {"2*(50%)",1.0}, {"5%^2",.0025}}})
            close(calculate(expression),expected);
        close(calculate("0%"),0); close(calculate("200/10%"),2000);
        close(calculate("2^50%"),std::sqrt(2.0)); close(calculate("sqrt(25%)"),.5);
        close(calculate("5!%"),1.2); close(calculate("100%!"),1); close(calculate("-5%^2"),-.0025);
        close(calculate("sin(pi/2)"),1); close(calculate("sen(30)",AngleUnit::degrees),.5);
        close(calculate("cos(60)",AngleUnit::degrees),.5); close(calculate("tan(45)",AngleUnit::degrees),1);
        close(calculate("asin(1)",AngleUnit::degrees),90); close(calculate("acos(0)"),std::numbers::pi / 2);
        close(calculate("atan(1)",AngleUnit::degrees),45); close(calculate("ln(e) + log(100)"),3);
        close(calculate("exp(1)"),std::numbers::e); close(calculate("sqrt(81)+abs(-2)"),11);
        close(calculate("1.25e2 + .5"),125.5); close(calculate("2 × (8 − 3) ÷ 5"),2);
        close(calculate("sin(π/2)"),1); close(calculate("SIN(PI/2)"),1);
        for (const auto invalid : {"","1/0","%50","7%3","1%0","50%/0","50%!","sqrt(-1)","ln(0)","asin(2)","(-1)!","3.5!","171!","tan(90)","2(3)","unknown(2)","1+","(1+2","1e","exp(10000)","1e9999","1,2","1;2"}) {
            bool rejected = false; try { (void)calculate(invalid,AngleUnit::degrees); } catch (const std::exception&) { rejected = true; }
            if (!rejected) throw std::runtime_error(std::string("Invalid expression accepted: ") + invalid);
        }
        bool deep_rejected = false; try { (void)calculate(std::string(140,'(') + '1' + std::string(140,')')); } catch (...) { deep_rejected = true; }
        if (!deep_rejected || format_calculation(-0.0) != "0" || format_calculation(1.25) != "1.25") throw std::runtime_error("Limits or result formatting changed");
        std::cout << "Scientific precedence, Android postfix percentages, real functions, angular units, factorial, constants, Unicode operators, errors and limits: PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
