#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lgaed::smo {
// Only operations with the two specified interpretations have an alternative.
// Ordinary evaluation consumes value; alternativa(expr) explicitly inspects it.
struct Number {
    double value = 0;
    std::optional<double> alternative;
};
// FP64 is a bounded support representation, not a proof about arbitrary SMO
// quantities. Nonfinite results and nonzero values lost to zero are rejected.

// One provisional notation policy, rather than substitutions across the lexer.
// Positional base 12 has twelve digits 0..9,#,~. @ is a whole-token alias for 12,
// not a thirteenth positional digit. The user's 0 always represents zero.
struct Notation {
    char zero = '0', ten = '#', eleven = '~', twelve_alias = '@';
};

[[nodiscard]] Number parse_number(std::string_view, Notation = {});
[[nodiscard]] std::string format_number(double, Notation = {});
// A zero factor executes the empty result 0; the other quantity is retained
// only as its explicit alternative. Division by zero instead executes the
// undivided numerator and retains 0 as its alternative. These are distinct.
[[nodiscard]] Number multiply(double, double);
[[nodiscard]] Number divide(double, double);
// Positive bases and integer exponents [-64,64] only in this prototype.
// At exponent zero the explicit rule executes 0 and retains the base as its
// alternative; this is not derived from the multiplication identity 1.
[[nodiscard]] Number direct_power(double base, double exponent);
[[nodiscard]] Number inverted_power(double base, double exponent);

struct Vec3 {
    double x = 0, y = 0, z = 0;
    bool operator==(const Vec3&) const = default;
};
struct Frame {
    std::vector<Vec3> vertices;
    std::vector<std::array<std::uint32_t, 3>> faces;
    Vec3 rotation;
    Vec3 color{1, 1, 1};
    bool draw_requested = false;
};

// Backend-independent, source-validated postfix IR. This export does NOT
// evaluate arithmetic or convert literal text through the legacy FP64 engine.
// The selected numerical backend must diagnose value/range errors, invalid
// calculated face indices and color bounds at the recorded source location.
enum class IrOpcode : std::uint32_t {
    literal=0, variable=1, dt=2, negate=3, positive=4, add=5, subtract=6,
    multiply=7, divide=8, direct_power=9, inverted_power=10, alternative=11,
    state=12, vertex=13, face=14, assign=15, rotation=16, color=17, draw=18
};
struct IrInstruction {
    IrOpcode opcode=IrOpcode::literal;
    // literal: literal-table index; variable/state/assign: state-table index.
    // All other operands are zero. Source positions are one based.
    std::uint32_t operand=0,line=1,column=1;
    bool operator==(const IrInstruction&)const=default;
};
static_assert(sizeof(IrInstruction)==16);
struct ProgramIr {
    Notation notation;
    // State slots follow declaration order. Each literal preserves its exact
    // unsigned source token; unary signs remain separate postfix operations.
    std::vector<std::string> states,literals;
    std::vector<IrInstruction> initial,update,draw;
    std::uint32_t max_stack=0,vertices=0,faces=0;
};
// Statements append argument expressions left-to-right, then their terminal:
// state/assign consume1, vertex/face/rotation/color consume3, draw consumes0.
// Every terminal leaves an empty stack. Expressions evaluate left before right;
// consumers must preserve primary/alternative values, not just scalar results.
// Existing source/node/statement/depth/geometry/state limits apply. A state
// initializer can read only earlier initialized states; dt is update-only.
// Compute-only native programs may omit a mesh. The original graphical
// laboratory keeps its authored geometry requirement by default.
[[nodiscard]] ProgramIr compile_ir(std::string_view source,Notation = {},bool require_geometry=true);

class Program {
public:
    [[nodiscard]] static Program compile(std::string_view source, Notation = {});
    Program(Program&&) noexcept;
    Program& operator=(Program&&) noexcept;
    ~Program();
    Program(const Program&) = delete;
    Program& operator=(const Program&) = delete;
    void reset();
    // frame runs only al_dibujar. step executes the authored al_actualizar and
    // then al_dibujar; dt is real elapsed seconds provided by the host. Pausing
    // omits step entirely; a step(0) still executes the authored statements.
    [[nodiscard]] Frame frame();
    [[nodiscard]] Frame step(double dt);
    [[nodiscard]] Number variable(std::string_view) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit Program(std::unique_ptr<Impl>);
};
} // namespace lgaed::smo
