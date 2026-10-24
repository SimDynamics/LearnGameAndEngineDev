#include "core/smo.hpp"
#include "native_build_config.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {
using namespace lgaed::smo;
void require(bool condition, std::string_view reason) { if (!condition) throw std::runtime_error(std::string(reason)); }
void close(double actual, double expected, std::string_view reason) {
    require(std::abs(actual - expected) <= std::max(1e-13, std::abs(expected) * 1e-12), reason);
}
void rejects(const std::function<void()>& action, std::string_view reason) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    require(rejected, reason);
}
std::string sample(std::string_view filename) {
    const auto path = std::filesystem::path(lgaed::build::source_root) / "native" / filename;
    std::ifstream input(path, std::ios::binary);
    require(input.is_open(), "The actual SMO sample is missing");
    std::ostringstream text; text << input.rdbuf(); return text.str();
}
void replace_once(std::string& source, std::string_view old, std::string_view replacement) {
    const auto at = source.find(old); require(at != source.npos, "The source mutation did not locate its real SMO instruction");
    source.replace(at, old.size(), replacement);
}
std::string mini(std::string_view states, std::string_view update, std::string_view draw = "rotacion(0, 0, giro); color(1, .6, .2); dibujar();") {
    return "vertice(-1, -1, 0); vertice(1, -1, 0); vertice(0, 1, 0); cara(0, 1, 2); " + std::string(states) +
        " al_actualizar { " + std::string(update) + " } al_dibujar { " + std::string(draw) + " }";
}
void numeration() {
    const std::pair<std::string_view, double> numbers[] = {
        {"0", 0}, {"9", 9}, {"#", 10}, {"~", 11}, {"10", 12}, {"@", 12}, {"-@", -12},
        {"11", 13}, {"1#", 22}, {"1~", 23}, {"20", 24}, {"100", 144}, {".6", .5}, {"0.3", .25},
        {"1.6", 1.5}, {"-1.6", -1.5}, {"1.3", 1.25}, {".#", 10. / 12.}, {".~", 11. / 12.}
    };
    for (const auto& [source, expected] : numbers) {
        const auto number = parse_number(source); close(number.value, expected, "Base-twelve magnitude is wrong");
        require(!number.alternative, "A literal invented a second interpretation");
    }
    require(format_number(12) == "10" && format_number(0) == "0" && format_number(10) == "#" && format_number(11) == "~",
        "Canonical positional formatting does not preserve zero and the twelve-digit alphabet");
    for (int value = -2048; value <= 2048; ++value)
        close(parse_number(format_number(value)).value, value, "Integer base-twelve formatting failed to round trip");
    require(format_number(.5) == "0.6" && format_number(1.25) == "1.3", "Finite fractional digits were written in decimal");
    const auto tiny = format_number(1e-30);
    require(tiny != "0" && tiny.find_first_of("123456789#~") != tiny.npos,
        "A nonzero value was displayed as the semantically different exact SMO zero");
    require(std::abs(parse_number(tiny).value / 1e-30 - 1) < 1e-10, "Small base-twelve formatting lost its real magnitude");
    require(format_number(std::numeric_limits<double>::denorm_min()) != "0", "The smallest FP64 value was displayed as exact zero");
    for (const auto text : {"", ".", "1@", "@1", "@@", "@.6", "1..2", "1a", "1e2", "12_", "+"})
        rejects([&] { (void)parse_number(text); }, "An invalid numeral or ambiguous alias was accepted");
    rejects([] { (void)parse_number("." + std::string(500, '0') + "1"); }, "A nonzero literal silently underflowed to SMO zero");
    Notation chosen; chosen.ten = '$'; chosen.eleven = '&'; chosen.twelve_alias = '?';
    close(parse_number("1$", chosen).value, 22, "The centralized notation cannot be adjusted");
    close(parse_number("?", chosen).value, 12, "The centralized twelve alias was ignored");
    require(format_number(11, chosen) == "&", "Formatting bypassed the centralized notation");
    for (const char bad : {'_', '\0', '\x01', '+', ' ', '/', '.', '0'}) {
        auto wrong = Notation{}; wrong.ten = bad;
        rejects([&] { (void)parse_number("0", wrong); }, "Notation accepted an identifier, control, comment or operator conflict");
    }
    auto wrong_zero = Notation{}; wrong_zero.zero = '@';
    rejects([&] { (void)parse_number("0", wrong_zero); }, "The user's explicit 0=cero rule was made provisional again");
    std::cout << "PASS SMO numeration: 0 fixed, #/~ digits, independent @ alias, positional fractions, configurable symbols and no false zero\n";
}
void arithmetic() {
    const std::array<int, 7> exponents{3, 2, 1, 0, -1, -2, -3};
    const std::array<double, 7> direct{8, 4, 2, 0, -2, -4, -8};
    const std::array<double, 7> inverted{.5, 1, 2, 0, -2, -1, -.5};
    for (std::size_t i = 0; i < exponents.size(); ++i) {
        const auto a = direct_power(2, exponents[i]), b = inverted_power(2, exponents[i]);
        close(a.value, direct[i], "The specified direct-power table changed");
        close(b.value, inverted[i], "The specified inverted-power table changed");
        if (exponents[i] == 0) {
            require(a.alternative && b.alternative && *a.alternative == 2 && *b.alternative == 2,
                "Exponent zero lost one of the specified 0/base results or used conventional 1");
        } else require(!a.alternative && !b.alternative, "An unambiguous power invented a pair");
    }
    for (int a = -5; a <= 5; ++a) {
        for (const auto result : {multiply(a, 0), multiply(0, a)})
            require(result.value == 0 && result.alternative && *result.alternative == a,
                "A zero factor lost the executed empty result or its explicit conserved-quantity alternative");
        const auto undivided = divide(a, 0);
        require(undivided.value == a && undivided.alternative && *undivided.alternative == 0,
            "Division by zero no longer preserves its undivided numerator separately from multiplication");
        for (int b = -5; b <= 5; ++b) {
            require(multiply(a, b).value == multiply(b, a).value, "The chosen default block product lost commutativity");
            if (a && b) {
                const auto sign = a < 0 || b < 0 ? -1 : 1;
                require(multiply(a, b).value == sign * std::abs(a * b), "Provisional negative-orientation multiplication changed");
                close(divide(a, b).value, sign * std::abs(static_cast<double>(a) / b), "Provisional subdivision sign changed");
                require(!multiply(a, b).alternative && !divide(a, b).alternative,
                    "Normal multiplication/division unnecessarily returned a pair");
            }
            for (int c = -3; c <= 3; ++c)
                require(multiply(multiply(a, b).value, c).value == multiply(a, multiply(b, c).value).value,
                    "The bounded default-product associativity experiment failed");
        }
    }
    require(multiply(-2, -2).value == -4 && divide(-2, -2).value == -1,
        "The experimental sign law was silently replaced with ordinary arithmetic");
    // This bounded domain is used by the particle lowering: a known
    // nonnegative coefficient avoids both-negative products, including zero.
    for(const auto a:{-5.,-.25,0.,.25,5.})for(const auto b:{0.,.125,1.,2.})
        require(multiply(a,b).value==a*b,"Known nonnegative particle coefficients lost their ordinary-product equivalence");
    // These are checks of the selected DEFAULT branch, not a theorem about
    // every interpretation, arbitrary reals, floating rounding or all of SMO.
    for (int base = 1; base <= 3; ++base) for (int a = 1; a <= 6; ++a) for (int b = 1; b <= 6; ++b)
        require(direct_power(base, a + b).value == multiply(direct_power(base, a).value, direct_power(base, b).value).value,
            "The bounded positive-integer direct-power product experiment failed");
    require(direct_power(2, 1).value != multiply(direct_power(2, 1).value, direct_power(2, 0).value).value,
        "The direct-power product-law counterexample at exponent zero was hidden");
    require(multiply(-2, 1 + -1).value != multiply(-2, 1).value + multiply(-2, -1).value,
        "The known distributivity counterexample was hidden");
    require(multiply(direct_power(2, 1).value, direct_power(2, -1).value).value != direct_power(2, 0).value,
        "The known negative-exponent product-law counterexample was hidden");
    require(divide(inverted_power(2, 0).value, 2).value != inverted_power(2, 1).value,
        "The inverted-power discontinuity at zero was silently conventionalized");
    for (const auto function : {direct_power, inverted_power}) {
        rejects([&] { (void)function(-2, 2); }, "An undefined negative-base generalization was silently adopted");
        rejects([&] { (void)function(0, 2); }, "An undefined zero-base generalization was silently adopted");
        rejects([&] { (void)function(2, .5); }, "Fractional exponents were invented without a definition");
        rejects([&] { (void)function(2, 65); }, "The declared prototype exponent budget was not enforced");
    }
    rejects([] { (void)multiply(std::numeric_limits<double>::max(), 2); }, "Overflow was accepted as a mathematical value");
    rejects([] { (void)multiply(1e-300, 1e-300); }, "A nonzero product underflowed into the semantically distinct exact zero");
    rejects([] { (void)divide(1e-300, 1e300); }, "A nonzero quotient underflowed into SMO zero");
    rejects([] { (void)direct_power(1e-300, 2); }, "A nonzero direct power underflowed into SMO zero");
    rejects([] { (void)inverted_power(1e300, 4); }, "An out-of-range inverted power silently became zero");
    rejects([] { (void)multiply(std::numeric_limits<double>::quiet_NaN(), 0); }, "NaN bypassed zero semantics");
    std::cout << "PASS SMO arithmetic: both specified power tables/zero branches, deterministic defaults, provisional signs, bounded laws and explicit counterexamples\n";
}
void interpreter() {
    const auto source = sample("smo_triangle.smo");
    auto triangle = Program::compile(source);
    const auto initial = triangle.frame();
    require(initial.draw_requested && initial.vertices.size() == 3 && initial.faces.size() == 1,
        "The authored SMO triangle did not provide its own geometry and draw request");
    require(initial.rotation == Vec3{} && triangle.frame().rotation == Vec3{},
        "Drawing without an update injected native animation");
    close(initial.color.y, .5, "SMO color .6 was treated as decimal");
    close(initial.color.z, 1. / 6., "SMO color .2 was treated as decimal");
    const auto advanced = triangle.step(.25);
    close(advanced.rotation.z, .25, "The authored update did not consume actual elapsed time");
    close(triangle.variable("giro").value, .25, "Rotation was native rather than an actual SMO state");
    auto faster_source = source; replace_once(faster_source, "estado velocidad = 1;", "estado velocidad = 2;");
    auto faster = Program::compile(faster_source); close(faster.step(.25).rotation.z, .5, "Changing actual SMO source failed to change movement");
    auto stopped_source = source; replace_once(stopped_source, "estado velocidad = 1;", "estado velocidad = 0;");
    replace_once(stopped_source, "estado giro = 0;", "estado giro = 1;");
    auto stopped = Program::compile(stopped_source);
    close(stopped.step(.25).rotation.z, 1, "A zero authored velocity created motion from an empty quantity");
    close(stopped.step(1).rotation.z, 1, "Repeated elapsed updates moved a zero-velocity SMO program");
    auto taller_source = source; replace_once(taller_source, "vertice(0, 1, 0);", "vertice(0, 2, 0);");
    auto taller = Program::compile(taller_source); require(taller.frame().vertices[2].y == 2 && initial.vertices[2].y == 1,
        "Changing actual SMO vertices failed to change the figure");
    auto no_draw_source = source; replace_once(no_draw_source, "dibujar();", "");
    auto no_draw = Program::compile(no_draw_source); require(!no_draw.frame().draw_requested && !no_draw.step(.25).draw_requested,
        "A native fixed animation drew without the SMO program's request");
    const auto zero_time = triangle.step(0);
    close(zero_time.rotation.z, .25, "A zero elapsed interval created motion through multiplication by zero");
    auto explicit_update = Program::compile(mini("estado giro = 0;", "giro = giro + 1;"));
    close(explicit_update.step(0).rotation.z, 1, "The runtime skipped authored updates at dt=0 instead of letting the host omit paused steps");
    triangle.reset(); require(triangle.frame().rotation == initial.rotation && triangle.variable("giro").value == 0,
        "Reset failed to restore the declared state");
    auto another = Program::compile(source);
    for (int i = 0; i < 100; ++i)
        require(triangle.step(.125).rotation == another.step(.125).rotation,
            "The same SMO inputs and elapsed intervals were not deterministic");
    auto cube = Program::compile(sample("smo_cube.smo"));
    const auto cube_frame = cube.frame();
    require(cube_frame.draw_requested && cube_frame.vertices.size() == 8 && cube_frame.faces.size() == 12,
        "The cube did not extend the same interpreter using authored geometry");
    const auto rotated = cube.step(.5); close(rotated.rotation.y, .25, "Cube state did not use its .6 base-twelve speed");
    close(rotated.rotation.x, .125, "Cube SMO did not control its second rotation axis");
    for (const auto ending : {std::string("\r\n"), std::string("\r")}) {
        std::string imported;
        for (const auto c : source) { if (c == '\n') imported += ending; else imported += c; }
        auto windows_source = Program::compile(imported);
        require(windows_source.step(.25).rotation == advanced.rotation,
            "A Windows line ending made a line comment swallow actual SMO instructions");
    }
    auto numeric = Program::compile(mini("estado giro = 0; estado diez = #; estado once = ~; estado doce = 10; estado alias = @; estado dos = pdir(2,0);",
        "giro = alternativa(dos);"));
    require(numeric.variable("diez").value == 10 && numeric.variable("once").value == 11 && numeric.variable("doce").value == 12 && numeric.variable("alias").value == 12,
        "The actual language lexer bypassed its numeral policy");
    require(numeric.variable("dos").value == 0 && numeric.variable("dos").alternative == 2,
        "A zero-power state lost one of its two interpretations");
    require(numeric.step(.25).rotation.z == 2, "Explicit alternativa did not expose the explanatory branch");
    auto zero_branches = Program::compile(mini("estado giro = 0; estado producto = 2 * 0; estado cociente = 2 / 0;",
        "giro = alternativa(producto);"));
    require(zero_branches.variable("producto").value == 0 && zero_branches.variable("producto").alternative == 2 &&
        zero_branches.variable("cociente").value == 2 && zero_branches.variable("cociente").alternative == 0,
        "Actual source conflated the distinct executed multiplication/division zero branches");
    close(zero_branches.step(.25).rotation.z, 2, "The retained product quantity was not explicitly accessible through alternativa");
    auto direct = Program::compile(mini("estado giro = 0;", "giro = pdir(2, -3) + pinv(2, 3);"));
    close(direct.step(.2).rotation.z, -7.5, "The interpreter substituted conventional power or sign rules");
    auto overflow = Program::compile(mini("estado giro = 0; estado cantidad = pdir(2, 54);", "giro = giro + 1; cantidad = cantidad * cantidad;"));
    bool rejected = false;
    for (int i = 0; i < 8; ++i) {
        const auto old_giro = overflow.variable("giro").value, old_cantidad = overflow.variable("cantidad").value;
        try { (void)overflow.step(.1); }
        catch (const std::exception&) { rejected = true; require(overflow.variable("giro").value == old_giro && overflow.variable("cantidad").value == old_cantidad,
                "A failed update partially committed the program state"); break; }
    }
    require(rejected, "A growing SMO state ignored FP64 overflow");
    const auto unchanged = another.variable("giro").value;
    rejects([&] { (void)another.step(-1); }, "Negative elapsed time was accepted");
    rejects([&] { (void)another.step(std::numeric_limits<double>::infinity()); }, "Nonfinite host time was accepted");
    require(another.variable("giro").value == unchanged, "Invalid host time altered the last valid state");
    rejects([&] { (void)another.variable("unknown"); }, "Unknown states were invented");
    std::cout << "PASS real SMO source: triangle then cube, numeric AST execution, changed vertices/speed, draw ownership, reset, deterministic state and transactional errors\n";
}
void diagnostics() {
    for (const auto update : {"giro = desconocido;", "otro = 1;", "dt = 1;", "giro = sin(1);", "giro = alternativa(1);", "giro = 1@;", "giro = @1;"})
        rejects([&] { auto p = Program::compile(mini("estado giro = 0;", update)); (void)p.step(.1); }, "An invalid program was silently accepted");
    rejects([] { (void)Program::compile(mini("estado giro = 0; estado giro = 1;", "")); }, "Duplicate state was accepted");
    rejects([] { (void)Program::compile(mini("estado dt = 0; estado giro = 0;", "")); }, "A host-time state collision was accepted");
    rejects([] { (void)Program::compile("int main() { return 0; }"); }, "C++ was accepted as SMO by changing its extension");
    rejects([] { (void)Program::compile("# This is not a comment\n" + mini("estado giro = 0;", "")); }, "# was treated as a comment rather than the chosen SMO digit");
    rejects([] { (void)Program::compile(mini("estado giro = 0;", "", "color(2,0,0); dibujar();")); }, "Out-of-range color was silently clamped");
    rejects([] { (void)Program::compile("vertice(0,0,0); cara(0,0,0); al_actualizar{} al_dibujar{}"); }, "A one-vertex mesh passed an at-least-three-vertices contract");
    for (const auto face : {"cara(0,1,3);", "cara(-1,1,2);", "cara(.6,1,2);"})
        rejects([&] { (void)Program::compile("vertice(0,0,0); vertice(1,0,0); vertice(0,1,0); " + std::string(face) + " al_actualizar{} al_dibujar{}"); },
            "An invalid original mesh index was accepted");
    rejects([] { (void)Program::compile(mini("estado giro = 0;", "giro = " + std::string(1000, '-') + "1;")); }, "Unbounded unary nesting was accepted");
    std::string chain = "giro = 1"; for (int i = 0; i < 1000; ++i) chain += "+1"; chain += ';';
    rejects([&] { (void)Program::compile(mini("estado giro = 0;", chain)); }, "Unbounded left-associative nesting was accepted");
    rejects([] { (void)Program::compile(std::string(65 * 1024, ' ')); }, "An oversized source bypassed its declared prototype budget");
    try { (void)Program::compile("\n\nvertice(0,0,0)\n"); require(false, "Malformed program was accepted"); }
    catch (const std::exception& error) {
        require(std::string_view(error.what()).find("Línea 4, columna 1") != std::string_view::npos,
            "The diagnostic omitted its real source line and column");
    }
    std::cout << "PASS SMO diagnostics: source locations, phases, names, geometry, explicit limits and invalid-language rejection\n";
}
void exported_ir() {
    const auto structural=[](const ProgramIr& ir) {
        std::uint32_t maximum=0;
        for(const auto* phase:{&ir.initial,&ir.update,&ir.draw}) {
            std::uint32_t stack=0;
            for(const auto& instruction:*phase) {
                require(instruction.line>0&&instruction.column>0,"IR source locations are not one based");
                switch(instruction.opcode) {
                case IrOpcode::literal:require(instruction.operand<ir.literals.size(),"IR literal slot is invalid");++stack;break;
                case IrOpcode::variable:require(instruction.operand<ir.states.size(),"IR state-read slot is invalid");++stack;break;
                case IrOpcode::dt:require(phase==&ir.update&&instruction.operand==0,"IR host time escaped the update phase");++stack;break;
                case IrOpcode::negate:case IrOpcode::positive:case IrOpcode::alternative:
                    require(stack>=1&&instruction.operand==0,"IR unary expression has no argument");break;
                case IrOpcode::add:case IrOpcode::subtract:case IrOpcode::multiply:case IrOpcode::divide:
                case IrOpcode::direct_power:case IrOpcode::inverted_power:
                    require(stack>=2&&instruction.operand==0,"IR binary expression has too few arguments");--stack;break;
                case IrOpcode::state:case IrOpcode::assign:
                    require(stack==1&&instruction.operand<ir.states.size(),"IR state terminal consumed the wrong expression");stack=0;break;
                case IrOpcode::vertex:case IrOpcode::face:case IrOpcode::rotation:case IrOpcode::color:
                    require(stack==3&&instruction.operand==0,"IR triple terminal changed argument order or stack shape");stack=0;break;
                case IrOpcode::draw:require(stack==0&&instruction.operand==0,"IR draw consumed an unexpected expression");break;
                default:throw std::runtime_error("Unknown exported IR opcode");
                }
                maximum=std::max(maximum,stack);
            }
            require(stack==0,"IR phase ended with a leftover value");
        }
        require(maximum==ir.max_stack,"IR maximum stack metadata is not the actual postfix stack bound");
    };
    for(const auto filename:{"smo_triangle.smo","smo_cube.smo"}) {
        const auto ir=compile_ir(sample(filename));structural(ir);
        require(ir.states==std::vector<std::string>{"giro","velocidad"},"IR slots do not follow state declaration order");
        const bool cube=std::string_view(filename)=="smo_cube.smo";
        require(ir.vertices==(cube?8u:3u)&&ir.faces==(cube?12u:1u),"IR metadata lost authored geometry");
        require(ir.draw.back().opcode==IrOpcode::draw&&ir.update.back().opcode==IrOpcode::assign,"IR phases lost their authored terminal");
        require(std::ranges::count_if(ir.update,[](const auto& op){return op.opcode==IrOpcode::dt;})==1,"IR host time was not exported as an independent input");
    }
    const auto source=mini("estado giro=0; estado par=2*0; estado usado=alternativa(par);",
        "giro = +giro + dt * pdir(2, -3) - pinv(2, 3) / 1;");
    const auto all=compile_ir(source);structural(all);
    const std::vector<IrOpcode> expected{IrOpcode::variable,IrOpcode::positive,IrOpcode::dt,IrOpcode::literal,IrOpcode::literal,
        IrOpcode::negate,IrOpcode::direct_power,IrOpcode::multiply,IrOpcode::add,IrOpcode::literal,IrOpcode::literal,
        IrOpcode::inverted_power,IrOpcode::literal,IrOpcode::divide,IrOpcode::subtract,IrOpcode::assign};
    std::vector<IrOpcode> actual;for(const auto& op:all.update)actual.push_back(op.opcode);
    require(actual==expected&&all.max_stack==4,"IR changed expression precedence, associativity, or left-before-right evaluation");
    std::array<bool,19> coverage{};for(const auto* phase:{&all.initial,&all.update,&all.draw})for(const auto& op:*phase)coverage[static_cast<unsigned>(op.opcode)]=true;
    require(std::ranges::all_of(coverage,[](bool covered){return covered;}),"The IR regression omitted an expression or statement opcode");
    require(all.states==std::vector<std::string>{"giro","par","usado"}&&all.update.front().operand==0&&all.update.back().operand==0,
        "IR assignment/read slots lost their declaration identity");
    const std::string positioned="// comentario\r\nestado giro = #;\r\nvertice(0,0,0); vertice(1,0,0); vertice(0,1,0); cara(0,1,2);\r\n"
        "al_actualizar {\r\n  giro = giro + dt;\r\n}\r\nal_dibujar { rotacion(0,0,giro); dibujar(); }\r\n";
    const auto positions=compile_ir(positioned);structural(positions);
    require(positions.initial[0].line==2&&positions.initial[0].column==15&&positions.literals[positions.initial[0].operand]=="#",
        "The exact literal lost its CRLF source coordinates");
    require(positions.update[2].opcode==IrOpcode::add&&positions.update[2].line==5&&positions.update[2].column==15&&
        positions.update[1].opcode==IrOpcode::dt&&positions.update[1].column==17&&positions.update.back().column==3,
        "IR operation/host-time/statement coordinates point to different source tokens");
    const auto raw_literal=[&](std::string text) {
        const auto program=compile_ir(mini("estado giro="+text+";",""));structural(program);
        require(std::ranges::find(program.literals,text)!=program.literals.end(),"IR rounded or rewrote a literal token before numerical execution");
    };
    raw_literal("0001.6000000000000000000000000000000000000000001");
    raw_literal("0."+std::string(180,'0')+"1"); //minimum representable F12
    raw_literal("1"+std::string(400,'0')); //grammar-valid; numerical range belongs to the backend
    const auto below_both_ranges="0."+std::string(1000,'0')+"1";
    raw_literal(below_both_ranges);
    rejects([&]{(void)Program::compile(mini("estado giro="+below_both_ranges+";",""));},
        "The legacy FP64 underflow probe did not distinguish literal export from evaluation");
    const Notation custom{'0','%','&','$'};
    const auto custom_ir=compile_ir(mini("estado giro=%+&+$;",""),custom);
    require(custom_ir.notation.ten=='%'&&custom_ir.notation.eleven=='&'&&custom_ir.notation.twelve_alias=='$'&&
        std::ranges::find(custom_ir.literals,"%")!=custom_ir.literals.end(),"IR discarded the original notation policy");
    for(const auto states:{"estado giro=despues; estado despues=1;","estado giro=giro;"})
        rejects([&]{(void)compile_ir(mini(states,""));},"IR allowed an initializer to read a state before initialization");
    rejects([]{(void)compile_ir("vertice(giro,0,0); estado giro=0; vertice(1,0,0); vertice(0,1,0); cara(0,1,2); al_actualizar{} al_dibujar{}");},
        "IR allowed geometry to read a later state declaration");
    for(const auto update:{"giro=desconocido;","otro=1;","dt=1;","giro=sin(1);","giro=1@;","giro=@1;","giro=.;","giro=1..2;"})
        rejects([&]{(void)compile_ir(mini("estado giro=0;",update));},"IR accepted an invalid phase/name/literal grammar");
    rejects([]{(void)compile_ir(mini("estado giro=dt;",""));},"IR exposed host time during initialization");
    rejects([]{(void)compile_ir(mini("estado giro=0;","","rotacion(dt,0,0);"));},"IR exposed host time while drawing");
    rejects([]{(void)compile_ir(mini("estado giro=0; estado giro=1;",""));},"IR silently merged duplicate states");
    rejects([]{(void)compile_ir("al_actualizar{} al_dibujar{}");},"IR omitted the minimum authored mesh contract");
    rejects([]{(void)compile_ir(mini("estado giro=0;","giro="+std::string(1000,'-')+"1;"));},"IR bypassed the parser's unary depth limit");
    rejects([]{(void)compile_ir(std::string(65*1024,' '));},"IR bypassed the shared source limit");
    const std::string mesh="vertice(0,0,0); vertice(1,0,0); vertice(0,1,0); cara(0,1,2);";
    std::string too_many_states=mesh;for(unsigned i=0;i<257;++i)too_many_states+="estado v"+std::to_string(i)+"=0;";
    rejects([&]{(void)compile_ir(too_many_states+"al_actualizar{} al_dibujar{}");},"IR bypassed the256-state limit");
    std::string too_many_vertices;for(unsigned i=0;i<1025;++i)too_many_vertices+="vertice(0,0,0);";
    rejects([&]{(void)compile_ir(too_many_vertices+"cara(0,1,2);al_actualizar{}al_dibujar{}");},"IR bypassed the1024-vertex limit");
    std::string too_many_faces="vertice(0,0,0);vertice(1,0,0);vertice(0,1,0);";for(unsigned i=0;i<2049;++i)too_many_faces+="cara(0,1,2);";
    rejects([&]{(void)compile_ir(too_many_faces+"al_actualizar{}al_dibujar{}");},"IR bypassed the2048-face limit");
    std::string too_many_statements=mesh+"al_actualizar{}al_dibujar{";for(unsigned i=0;i<4097;++i)too_many_statements+="dibujar();";
    rejects([&]{(void)compile_ir(too_many_statements+"}");},"IR bypassed the4096-statement limit");
    std::string expression="1";for(unsigned i=0;i<100;++i)expression+="+1";
    std::string too_many_nodes=mesh+"estado giro=0;al_actualizar{";for(unsigned i=0;i<41;++i)too_many_nodes+="giro="+expression+";";
    rejects([&]{(void)compile_ir(too_many_nodes+"}al_dibujar{}");},"IR bypassed the8192-expression-node limit");
    // Runtime-value constraints deliberately remain in the numerical backend:
    // no FP64 evaluation/constant folding is smuggled into the export path.
    structural(compile_ir(mini("estado giro=0;","giro=alternativa(1);","color(2,0,0);dibujar();")));
    std::cout<<"PASS validated SMO postfix IR: exact raw literals, all phases/opcodes, ordered state slots, source coordinates and shared limits; numerical evaluation remains backend-owned\n";
}
} // namespace

int main() {
    try { numeration(); arithmetic(); interpreter(); diagnostics(); exported_ir(); return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL SMO: " << error.what() << '\n'; return 1; }
}
