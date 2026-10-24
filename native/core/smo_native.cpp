#include "core/smo_native.hpp"
#include <algorithm>
#include <bit>
#include <cctype>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace lgaed::smo {
namespace {
static_assert(sizeof(float)==4&&std::numeric_limits<float>::is_iec559&&std::numeric_limits<float>::digits==24&&std::numeric_limits<float>::radix==2,"El perfil real requiere IEEE754 binary32 nativo.");
constexpr auto minimum=std::numeric_limits<std::int32_t>::min(),maximum=std::numeric_limits<std::int32_t>::max();
[[noreturn]]void fail(std::uint32_t line,std::uint32_t column,std::string_view reason){throw std::invalid_argument("Línea "+std::to_string(line)+", columna "+std::to_string(column)+": "+std::string(reason));}
void notation_valid(Notation n){const auto symbol=[](char c){return static_cast<unsigned char>(c)<128&&std::isprint(static_cast<unsigned char>(c))&&!std::isalnum(static_cast<unsigned char>(c))&&
    !std::isspace(static_cast<unsigned char>(c))&&std::string_view("_+-*/=.,;(){}\"'\\").find(c)==std::string_view::npos;};
    if(n.zero!='0'||!symbol(n.ten)||!symbol(n.eleven)||!symbol(n.twelve_alias)||n.ten==n.eleven||n.ten==n.twelve_alias||n.eleven==n.twelve_alias)throw std::invalid_argument("Notación SMO inválida; cero sigue siendo0.");}
std::uint32_t magnitude(std::string_view text,Notation n){if(text.empty())throw std::invalid_argument("Faltan cifras base12.");if(text.size()==1&&text.front()==n.twelve_alias)return 12;
    std::uint64_t value=0;bool point=false,seen=false;for(char c:text){if(c=='.'){if(point)throw std::invalid_argument("El literal contiene dos puntos fraccionarios.");point=true;continue;}
        const int digit=c>='0'&&c<='9'?c-'0':c==n.ten?10:c==n.eleven?11:-1;
        if(digit<0)throw std::invalid_argument("Cifra base12 inválida o alias no independiente.");seen=true;
        if(point){if(digit!=0)throw std::invalid_argument("Este perfil entero nativo no admite cantidades fraccionarias.");}
        else{value=value*12+static_cast<unsigned>(digit);if(value>0x80000000ull)throw std::invalid_argument("El literal supera el rango int32 del perfil nativo.");}}
    if(!seen)throw std::invalid_argument("El punto necesita al menos una cifra.");return static_cast<std::uint32_t>(value);}
struct Calculation {std::int32_t value=0;NativeError error=NativeError::none;};
// Compilation-only unsigned multiprecision integer. Its limbs are binary
// words, never runtime quantities and never an intermediate decimal format.
struct Big {
    std::vector<std::uint32_t> words;
    explicit Big(std::uint64_t value=0){if(value){words.push_back(static_cast<std::uint32_t>(value));if(value>>32)words.push_back(static_cast<std::uint32_t>(value>>32));}}
    void trim(){while(!words.empty()&&words.back()==0)words.pop_back();}
    bool empty()const{return words.empty();}
    int bits()const{return empty()?0:static_cast<int>((words.size()-1)*32+32-std::countl_zero(words.back()));}
    void multiply(std::uint32_t factor){std::uint64_t carry=0;for(auto& word:words){const auto next=static_cast<std::uint64_t>(word)*factor+carry;word=static_cast<std::uint32_t>(next);carry=next>>32;}if(carry)words.push_back(static_cast<std::uint32_t>(carry));trim();}
    void add(std::uint32_t digit){std::uint64_t carry=digit;for(auto& word:words){const auto next=static_cast<std::uint64_t>(word)+carry;word=static_cast<std::uint32_t>(next);carry=next>>32;if(!carry)break;}if(carry)words.push_back(static_cast<std::uint32_t>(carry));}
    Big shift(unsigned count)const{if(empty())return Big{};Big result;const auto whole=count/32,part=count%32;result.words.assign(words.size()+whole+1,0);std::uint64_t carry=0;
        for(std::size_t i=0;i<words.size();++i){const auto next=(static_cast<std::uint64_t>(words[i])<<part)|carry;result.words[i+whole]=static_cast<std::uint32_t>(next);carry=next>>32;}result.words[words.size()+whole]=static_cast<std::uint32_t>(carry);result.trim();return result;}
};
int compare(const Big& a,const Big& b){if(a.words.size()!=b.words.size())return a.words.size()<b.words.size()?-1:1;for(std::size_t i=a.words.size();i>0;--i)if(a.words[i-1]!=b.words[i-1])return a.words[i-1]<b.words[i-1]?-1:1;return 0;}
void subtract(Big& a,const Big& b){std::uint64_t borrow=0;for(std::size_t i=0;i<a.words.size();++i){const std::uint64_t left=a.words[i],right=(i<b.words.size()?static_cast<std::uint64_t>(b.words[i]):0)+borrow;a.words[i]=static_cast<std::uint32_t>(left-right);borrow=left<right?1u:0u;}a.trim();}
std::uint32_t quotient(Big& remainder,const Big& denominator){std::uint32_t result=0;const auto top=remainder.bits()-denominator.bits();if(top>31)throw std::logic_error("Cociente del redondeo binario fuera de sus límites.");
    for(int bit=top;bit>=0;--bit){const auto term=denominator.shift(static_cast<unsigned>(bit));if(compare(remainder,term)>=0){subtract(remainder,term);result|=std::uint32_t{1}<<bit;}}return result;}
std::uint32_t rational_binary32(const Big& numerator,const Big& denominator,bool negative,bool tail=false){const auto sign=negative?0x80000000u:0u;
    if(numerator.empty()){if(tail)throw std::invalid_argument("Underflow: el literal no nulo se redondea a cero.");return sign;}
    int exponent=numerator.bits()-denominator.bits();if((exponent>=0&&compare(numerator,denominator.shift(static_cast<unsigned>(exponent)))<0)||
        (exponent<0&&compare(numerator.shift(static_cast<unsigned>(-exponent)),denominator)<0))--exponent;
    if(exponent>127)throw std::invalid_argument("El literal supera el rango finito binary32.");const auto scale=exponent>=-126?23-exponent:149;
    Big remainder=scale>=0?numerator.shift(static_cast<unsigned>(scale)):numerator;const auto divisor=scale>=0?denominator:denominator.shift(static_cast<unsigned>(-scale));
    auto mantissa=quotient(remainder,divisor);const auto order=compare(remainder.shift(1),divisor);
    if(order>0||(order==0&&(tail||(mantissa&1u))))++mantissa;
    if(exponent<-126){if(mantissa>=0x800000u)return sign|0x00800000u;throw std::invalid_argument("Underflow: este perfil admite normales binary32 y cero, no subnormales.");}
    if(mantissa==0x1000000u){mantissa>>=1;++exponent;}if(exponent>127)throw std::invalid_argument("El literal redondeado supera el rango finito binary32.");
    return sign|(static_cast<std::uint32_t>(exponent+127)<<23)|(mantissa&0x7fffffu);}
NativeError real_encoding(std::uint32_t bits){const auto exponent=(bits>>23)&255u;return exponent==255u?NativeError::nonfinite:exponent==0u&&(bits&0x7fffffu)?NativeError::underflow:NativeError::none;}
struct RealCalculation {std::uint32_t bits=0;NativeError error=NativeError::none;};
RealCalculation real_result(float value,bool nonzero){const auto bits=std::bit_cast<std::uint32_t>(value);auto error=real_encoding(bits);if(error==NativeError::none&&nonzero&&(bits&0x7fffffffu)==0u)error=NativeError::underflow;return {error==NativeError::none?bits:0u,error};}
RealCalculation real_calculate(NativeOpcode op,std::uint32_t ua,std::uint32_t ub=0){auto error=real_encoding(ua);if(error!=NativeError::none)return {0,error};
    if(op==NativeOpcode::positive)return {ua,{}};if(op==NativeOpcode::negate)return {ua^0x80000000u,{}};error=real_encoding(ub);if(error!=NativeError::none)return {0,error};
    const auto a=std::bit_cast<float>(ua),b=std::bit_cast<float>(ub);const auto ma=ua&0x7fffffffu,mb=ub&0x7fffffffu;
    switch(op){case NativeOpcode::add:{volatile float result=a+b;return real_result(result,!(ma==mb&&(((ua^ub)&0x80000000u)||ma==0u)));}
    case NativeOpcode::subtract:{volatile float result=a-b;return real_result(result,!(ua==ub||(ma==0u&&mb==0u)));}
    case NativeOpcode::multiply:{volatile float result=a*b;return real_result(result,ma!=0u&&mb!=0u);}
    case NativeOpcode::divide:{if(mb==0u)return {0,NativeError::division_zero};volatile float result=a/b;return real_result(result,ma!=0u);}
    case NativeOpcode::power:{if(b<-64.0f||b>64.0f)return {0,NativeError::exponent};const auto degree=static_cast<std::int32_t>(b);if(static_cast<float>(degree)!=b)return {0,NativeError::exponent};
        std::uint32_t base=ua,result=0x3f800000u;auto exponent=static_cast<std::uint32_t>(degree<0?-degree:degree);
        if(degree<0){const auto inverse=real_calculate(NativeOpcode::divide,result,base);if(inverse.error!=NativeError::none)return inverse;base=inverse.bits;}
        for(unsigned bit=0;bit<7;++bit){if(exponent&1u){const auto next=real_calculate(NativeOpcode::multiply,result,base);if(next.error!=NativeError::none)return next;result=next.bits;}exponent>>=1;
            if(exponent){const auto next=real_calculate(NativeOpcode::multiply,base,base);if(next.error!=NativeError::none)return next;base=next.bits;}}return {result,{}};}
    default:throw std::logic_error("Operación binary32 no reconocida.");}}
Calculation multiply_checked(std::int32_t a,std::int32_t b){const auto product=static_cast<std::int64_t>(a)*b;
    return product<minimum||product>maximum?Calculation{0,NativeError::overflow}:Calculation{static_cast<std::int32_t>(product),NativeError::none};}
Calculation calculate(NativeOpcode op,std::int32_t a,std::int32_t b=0){std::int64_t value=0;
    switch(op){case NativeOpcode::positive:return {a,{}};
    case NativeOpcode::negate:if(a==minimum)return {0,NativeError::overflow};return {-a,{}};
    case NativeOpcode::add:value=static_cast<std::int64_t>(a)+b;break;
    case NativeOpcode::subtract:value=static_cast<std::int64_t>(a)-b;break;
    case NativeOpcode::multiply:return multiply_checked(a,b);
    case NativeOpcode::divide:if(b==0)return {0,NativeError::division_zero};if(a==minimum&&b==-1)return {0,NativeError::overflow};
        if(a%b!=0)return {0,NativeError::fraction};return {a/b,{}};
    case NativeOpcode::power:{if(b<0||b>64)return {0,NativeError::exponent};std::int32_t result=1;
        for(std::int32_t i=0;i<b;++i){const auto next=multiply_checked(result,a);if(next.error!=NativeError::none)return next;result=next.value;}return {result,{}};}
    default:throw std::logic_error("Operación de cálculo nativo no reconocida.");}
    return value<minimum||value>maximum?Calculation{0,NativeError::overflow}:Calculation{static_cast<std::int32_t>(value),NativeError::none};}
bool arithmetic(NativeOpcode op){return op>=NativeOpcode::positive&&op<=NativeOpcode::power;}
struct Builder {
    const ProgramIr& source;NativeAction action;NativeScalarProfile profile;NativeGraph graph;std::vector<std::uint32_t> stack,states;
    std::vector<std::optional<std::uint32_t>> constants;
    std::map<std::tuple<NativeOpcode,std::uint32_t,std::uint32_t,std::uint32_t>,std::uint32_t> common;
    std::uint32_t vertex=0,face=0;
    Builder(const ProgramIr& ir,NativeAction a,NativeScalarProfile p):source(ir),action(a),profile(p),states(ir.states.size(),native_no_value){}
    std::uint32_t emit(NativeOpcode op,std::array<std::uint32_t,3> args,std::uint32_t immediate,const IrInstruction& where){
        if(op==NativeOpcode::positive)return args[0];
        if(arithmetic(op)&&constants.at(args[0])&&(args[1]==native_no_value||constants.at(args[1]))){const auto a=*constants[args[0]],b=args[1]==native_no_value?0u:*constants[args[1]];
            RealCalculation result;if(profile==NativeScalarProfile::ordinary_binary32)result=real_calculate(op,a,b);else{const auto integer=calculate(op,std::bit_cast<std::int32_t>(a),std::bit_cast<std::int32_t>(b));result={std::bit_cast<std::uint32_t>(integer.value),integer.error};}
            // Constant failures stay checked nodes: they must report the same
            // first source error at execution, not disappear through folding.
            if(result.error==NativeError::none)return emit(NativeOpcode::constant,{native_no_value,native_no_value,native_no_value},result.bits,where);}
        const auto key=std::tuple{op,args[0],args[1],immediate};const bool reusable=op==NativeOpcode::constant||op==NativeOpcode::load_state||op==NativeOpcode::load_dt||arithmetic(op);
        if(reusable)if(const auto found=common.find(key);found!=common.end())return found->second;
        NativeInstruction node;node.opcode=op;node.arguments=args;node.immediate=immediate;node.line=where.line;node.column=where.column;
        const bool scalar=op<=NativeOpcode::power;if(scalar){node.result=graph.value_count++;constants.push_back(op==NativeOpcode::constant?std::optional(immediate):std::nullopt);}
        graph.instructions.push_back(node);if(reusable)common.emplace(key,node.result);return node.result;}
    std::uint32_t take(const IrInstruction& where){if(stack.empty())fail(where.line,where.column,"La pila IR nativa no contiene el argumento requerido.");auto id=stack.back();stack.pop_back();return id;}
    void sequence(const std::vector<IrInstruction>& instructions){for(std::size_t at=0;at<instructions.size();++at){const auto& node=instructions[at];const auto none=native_no_value;
        if(node.opcode==IrOpcode::literal){std::uint32_t value=0;try{value=profile==NativeScalarProfile::ordinary_binary32?native_parse_binary32(source.literals.at(node.operand),source.notation):magnitude(source.literals.at(node.operand),source.notation);}catch(const std::exception& error){fail(node.line,node.column,error.what());}
            if(profile==NativeScalarProfile::ordinary_i32&&value==0x80000000u){if(at+1>=instructions.size()||instructions[at+1].opcode!=IrOpcode::negate)fail(node.line,node.column,"La magnitud2^31 sólo es válida directamente bajo menos unario para representar INT_MIN.");++at;}
            stack.push_back(emit(NativeOpcode::constant,{none,none,none},value,node));continue;}
        if(node.opcode==IrOpcode::variable){if(node.operand>=states.size())fail(node.line,node.column,"Estado nativo desconocido.");auto& value=states[node.operand];
            if(value==none){if(action==NativeAction::reset)fail(node.line,node.column,"El estado todavía no está inicializado.");value=emit(NativeOpcode::load_state,{none,none,none},node.operand,node);}stack.push_back(value);continue;}
        if(node.opcode==IrOpcode::dt){stack.push_back(emit(NativeOpcode::load_dt,{none,none,none},0,node));continue;}
        if(node.opcode==IrOpcode::inverted_power||node.opcode==IrOpcode::alternative)fail(node.line,node.column,"pinv y alternativa todavía no forman parte del perfil entero ordinario nativo.");
        if(node.opcode==IrOpcode::positive||node.opcode==IrOpcode::negate){const auto a=take(node);stack.push_back(emit(node.opcode==IrOpcode::positive?NativeOpcode::positive:NativeOpcode::negate,{a,none,none},0,node));continue;}
        if(node.opcode>=IrOpcode::add&&node.opcode<=IrOpcode::direct_power){const auto b=take(node),a=take(node);NativeOpcode op=NativeOpcode::add;
            switch(node.opcode){case IrOpcode::add:op=NativeOpcode::add;break;case IrOpcode::subtract:op=NativeOpcode::subtract;break;case IrOpcode::multiply:op=NativeOpcode::multiply;break;case IrOpcode::divide:op=NativeOpcode::divide;break;default:op=NativeOpcode::power;break;}
            stack.push_back(emit(op,{a,b,none},0,node));continue;}
        if(node.opcode==IrOpcode::state||node.opcode==IrOpcode::assign){const auto value=take(node);if(node.operand>=states.size())fail(node.line,node.column,"Estado nativo fuera de rango.");states[node.operand]=value;(void)emit(NativeOpcode::store_state,{value,none,none},node.operand,node);}
        else if(node.opcode==IrOpcode::draw)(void)emit(NativeOpcode::draw,{none,none,none},0,node);
        else{const auto z=take(node),y=take(node),x=take(node);NativeOpcode op;std::uint32_t index=0;
            switch(node.opcode){case IrOpcode::vertex:op=NativeOpcode::vertex;index=vertex++;break;case IrOpcode::face:op=NativeOpcode::face;index=face++;break;
            case IrOpcode::rotation:op=NativeOpcode::rotation;break;case IrOpcode::color:op=NativeOpcode::color;break;default:fail(node.line,node.column,"Instrucción IR no admitida por el compilador nativo.");}
            (void)emit(op,{x,y,z},index,node);}
        if(!stack.empty())fail(node.line,node.column,"Una sentencia nativa dejó argumentos sin consumir.");}
        if(!stack.empty())throw std::logic_error("La fase nativa terminó con una expresión pendiente.");}
    NativeGraph finish(){if(action==NativeAction::reset)sequence(source.initial);else if(action==NativeAction::step)sequence(source.update);sequence(source.draw);return std::move(graph);}
};
// Stable source/compiler-policy identity, without a platform crypto dependency.
std::string sha256(std::string_view source){static constexpr std::array<std::uint32_t,64> k{
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    std::vector<std::uint8_t> data(source.begin(),source.end());const auto bits=static_cast<std::uint64_t>(data.size())*8;data.push_back(0x80);while(data.size()%64!=56)data.push_back(0);for(int i=7;i>=0;--i)data.push_back(static_cast<std::uint8_t>(bits>>(i*8)));
    std::array<std::uint32_t,8> h{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    for(std::size_t offset=0;offset<data.size();offset+=64){std::array<std::uint32_t,64>w{};for(unsigned i=0;i<16;++i)for(unsigned b=0;b<4;++b)w[i]=(w[i]<<8)|data[offset+i*4+b];
        for(unsigned i=16;i<64;++i){const auto x=w[i-15],y=w[i-2];w[i]=w[i-16]+(std::rotr(x,7)^std::rotr(x,18)^(x>>3))+w[i-7]+(std::rotr(y,17)^std::rotr(y,19)^(y>>10));}
        auto a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],z=h[7];for(unsigned i=0;i<64;++i){const auto t1=z+(std::rotr(e,6)^std::rotr(e,11)^std::rotr(e,25))+((e&f)^((~e)&g))+k[i]+w[i];const auto t2=(std::rotr(a,2)^std::rotr(a,13)^std::rotr(a,22))+((a&b)^(a&c)^(b&c));z=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=z;}
    std::ostringstream out;out<<std::hex<<std::setfill('0');for(auto word:h)out<<std::setw(8)<<word;return out.str();}
std::string hex(std::uint32_t value){std::ostringstream out;out<<"0x"<<std::hex<<std::setw(8)<<std::setfill('0')<<value<<'u';return out.str();}
std::string id(std::uint32_t value){return "n"+std::to_string(value);}
std::optional<std::uint32_t> constant_real_index(std::uint32_t bits,std::uint32_t limit){const auto magnitude=bits&0x7fffffffu;
    if(!magnitude)return limit?std::optional(0u):std::nullopt;
    if((bits&0x80000000u)||real_encoding(bits)!=NativeError::none)return std::nullopt;
    const auto exponent=static_cast<int>((bits>>23u)&255u)-127;if(exponent<0||exponent>31)return std::nullopt;
    const auto significand=(bits&0x7fffffu)|0x800000u;std::uint32_t value=0;
    if(exponent>=23)value=significand<<static_cast<unsigned>(exponent-23);
    else{const auto shift=static_cast<unsigned>(23-exponent);if(significand&((1u<<shift)-1u))return std::nullopt;value=significand>>shift;}
    return value<limit?std::optional(value):std::nullopt;}
std::array<bool,3> reset_buffer_coverage(const NativeGraph& graph,const std::array<std::uint32_t,3>& elements){
    // These are the existing validated-program bounds, not new restrictions.
    // For an unvalidated descriptor, conservatively retain every fill.
    if(elements[0]>65536u||elements[1]>1024u||elements[2]>2048u)return {};
    std::array<std::vector<bool>,3> written{std::vector<bool>(elements[0]),std::vector<bool>(elements[1]),std::vector<bool>(elements[2])};
    std::array<std::uint32_t,3> covered{};
    for(const auto& node:graph.instructions){unsigned buffer=0;
        switch(node.opcode){case NativeOpcode::store_state:buffer=0;break;case NativeOpcode::vertex:buffer=1;break;case NativeOpcode::face:buffer=2;break;default:continue;}
        if(node.immediate>=elements[buffer])return {};
        if(!written[buffer][node.immediate]){written[buffer][node.immediate]=true;++covered[buffer];}}
    return {covered[0]==elements[0],covered[1]==elements[1],covered[2]==elements[2]};}
struct ConstantGeometryTable {std::vector<std::array<std::uint32_t,4>> rows;std::array<std::uint32_t,2> counts{};};
ConstantGeometryTable constant_geometry_table(const NativeGraph& graph,bool real,std::uint32_t vertices,std::uint32_t faces){
    constexpr std::uint32_t minimum_rows=64,maximum_rows=4096;
    std::vector<std::optional<std::uint32_t>> known(graph.value_count);std::array<bool,2> valid{true,true};
    std::array<std::uint32_t,2> counts{};std::array<std::vector<std::array<std::uint32_t,4>>,2> rows;
    for(const auto& node:graph.instructions){if(node.opcode==NativeOpcode::constant){if(!real||real_encoding(node.immediate)==NativeError::none)known.at(node.result)=node.immediate;}
        else if(node.opcode==NativeOpcode::positive)known.at(node.result)=known.at(node.arguments[0]);
        if(node.opcode!=NativeOpcode::vertex&&node.opcode!=NativeOpcode::face)continue;
        const unsigned buffer=node.opcode==NativeOpcode::vertex?0u:1u;++counts[buffer];
        if(node.immediate>=(buffer==0?vertices:faces)){valid[buffer]=false;continue;}
        std::array<std::uint32_t,4> row{node.immediate*3u,0,0,0};bool proved=true;
        for(unsigned axis=0;axis<3;++axis){const auto bits=known.at(node.arguments[axis]);if(!bits){proved=false;break;}
            if(buffer==0)row[axis+1]=*bits;
            else{const auto index=real?constant_real_index(*bits,vertices):(*bits<vertices?std::optional(*bits):std::nullopt);
                if(!index){proved=false;break;}row[axis+1]=*index;}}
        valid[buffer]=valid[buffer]&&proved;
        if(proved&&rows[buffer].size()<maximum_rows)rows[buffer].push_back(row);}
    ConstantGeometryTable result;const auto total=(valid[0]?counts[0]:0u)+(valid[1]?counts[1]:0u);
    // One ICB holds at most4096uint4. Exceeding this optimization budget falls
    // back to ordinary stores; it never restricts the accepted program graph.
    // https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm
    if(total<minimum_rows||total>maximum_rows)return result;
    result.rows.reserve(total);for(unsigned buffer=0;buffer<2;++buffer)if(valid[buffer]){result.counts[buffer]=counts[buffer];
        result.rows.insert(result.rows.end(),rows[buffer].begin(),rows[buffer].end());}return result;}
std::string shaders(const NativeProgram& program,NativeAction action,bool glsl){if(static_cast<unsigned>(action)>2)throw std::invalid_argument("Acción nativa inexistente.");
    if(program.scalarProfile!=NativeScalarProfile::ordinary_i32&&program.scalarProfile!=NativeScalarProfile::ordinary_binary32)throw std::invalid_argument("Perfil escalar nativo desconocido.");
    const bool real=program.scalarProfile==NativeScalarProfile::ordinary_binary32;
    const auto& graph=program.actions[static_cast<unsigned>(action)];
    const auto geometry=glsl?ConstantGeometryTable{}:constant_geometry_table(graph,real,program.ir.vertices,program.ir.faces);
    std::ostringstream out;if(glsl)out<<R"(#version 430
#define NI(x) int(x)
#define NU(x) uint(x)
#define NDIAG uvec4
layout(std140,binding=8)uniform Params{uvec4 parameters[4];};
layout(std430,binding=0)buffer B0{uint states[];};layout(std430,binding=1)buffer B1{uint vertices[];};
layout(std430,binding=2)buffer B2{uint faces[];};layout(std430,binding=3)buffer B3{uint frames[];};
layout(std430,binding=4)buffer B4{uint status[];};layout(std430,binding=5)buffer B5{uint times[];};
)";else out<<R"(#define NI(x) asint(x)
#define NU(x) asuint(x)
#define NDIAG uint4
cbuffer Params:register(b0){uint4 parameters[4];};
RWByteAddressBuffer B0:register(u0);RWByteAddressBuffer B1:register(u1);RWByteAddressBuffer B2:register(u2);
RWByteAddressBuffer B3:register(u3);RWByteAddressBuffer B4:register(u4);RWByteAddressBuffer B5:register(u5);
)";
    if(real)out<<(glsl?"#define NF(x) uintBitsToFloat(x)\n#define NB(x) floatBitsToUint(x)\n#define NFIRST(x) findMSB(x)\nlayout(std430,binding=6)buffer B6{uint elapsed[];};\n":
        "#define NF(x) asfloat(x)\n#define NB(x) asuint(x)\n#define NFIRST(x) firstbithigh(x)\nRWByteAddressBuffer B6:register(u6);\n");
    for(unsigned buffer=0;buffer<(real?7u:6u);++buffer){const std::array<const char*,7> names{"states","vertices","faces","frames","status","times","elapsed"};
        out<<"uint r"<<buffer<<"(uint p){return "<<(glsl?std::string(names[buffer])+"[p]":"B"+std::to_string(buffer)+".Load(p*4u)")<<";}\n";
        out<<"void w"<<buffer<<"(uint p,uint v){"<<(glsl?std::string(names[buffer])+"[p]=v":"B"+std::to_string(buffer)+".Store(p*4u,v)")<<";}\n";}
    out<<R"(void bad(inout NDIAG diagnostic,uint code,uint sourceLine,uint sourceColumn,uint instruction){if(diagnostic.x==0u)diagnostic=NDIAG(code,sourceLine,sourceColumn,instruction);}
void commit_error(uint s,NDIAG diagnostic){w4(s,diagnostic.x);w4(s+1u,diagnostic.y);w4(s+2u,diagnostic.z);w4(s+3u,diagnostic.w);}
)";
    if(!real)out<<R"(int nneg(int a,inout NDIAG diagnostic,uint l,uint c,uint p){uint bits=NU(a);if(bits==0x80000000u){bad(diagnostic,1u,l,c,p);return 0;}return NI(0u-bits);}
int nadd(int a,int b,inout NDIAG diagnostic,uint l,uint c,uint p){uint x=NU(a),y=NU(b),v=x+y;if(((~(x^y)&(x^v))&0x80000000u)!=0u){bad(diagnostic,1u,l,c,p);return 0;}return NI(v);}
int nsub(int a,int b,inout NDIAG diagnostic,uint l,uint c,uint p){uint x=NU(a),y=NU(b),v=x-y;if((((x^y)&(x^v))&0x80000000u)!=0u){bad(diagnostic,1u,l,c,p);return 0;}return NI(v);}
uint mag(int a){uint bits=NU(a);return a<0?0u-bits:bits;}
int nmul(int a,int b,inout NDIAG diagnostic,uint l,uint c,uint p){uint x=mag(a),y=mag(b),limit=((a<0)!=(b<0))?0x80000000u:0x7fffffffu;
uint safe=y==0u?1u:y;if(y!=0u&&x>limit/safe){bad(diagnostic,1u,l,c,p);return 0;}uint product=x*y;return NI(((a<0)!=(b<0))?0u-product:product);}
int ndiv(int a,int b,inout NDIAG diagnostic,uint l,uint c,uint p){bool valid=b!=0&&!(NU(a)==0x80000000u&&b==NI(0xffffffffu));
int safeA=valid?a:0,safeB=valid?b:1;uint divisor=mag(safeB);if(!valid){bad(diagnostic,b==0?2u:1u,l,c,p);return 0;}
if(mag(safeA)%divisor!=0u){bad(diagnostic,3u,l,c,p);return 0;}return safeA/safeB;}
int npow(int a,int b,inout NDIAG diagnostic,uint l,uint c,uint p){if(b<0||b>64){bad(diagnostic,4u,l,c,p);return 0;}uint exponent=uint(b);int result=1,base=a;
for(uint bit=0u;bit<7u;++bit){if((exponent&1u)!=0u){result=nmul(result,base,diagnostic,l,c,p);if(diagnostic.x!=0u)return 0;}exponent>>=1u;
if(exponent!=0u){base=nmul(base,base,diagnostic,l,c,p);if(diagnostic.x!=0u)return 0;}}return result;}
)";
    else out<<R"(uint nencoding(uint bits){uint exponent=(bits>>23u)&255u;uint result=0u;if(exponent==255u)result=9u;else if(exponent==0u&&(bits&0x7fffffu)!=0u)result=10u;return result;}
float nload(uint bits,inout NDIAG diagnostic,uint l,uint c,uint p){uint error=nencoding(bits);if(error!=0u){bad(diagnostic,error,l,c,p);return NF(0u);}return NF(bits);}
float nfinish(float value,bool nonzero,inout NDIAG diagnostic,uint l,uint c,uint p){uint bits=NB(value),error=nencoding(bits);if(error==0u&&nonzero&&(bits&0x7fffffffu)==0u)error=10u;
if(error!=0u){bad(diagnostic,error,l,c,p);return NF(0u);}return value;}
float nneg(float a,inout NDIAG diagnostic,uint l,uint c,uint p){return NF(NB(a)^0x80000000u);}
float nadd(float a,float b,inout NDIAG diagnostic,uint l,uint c,uint p){precise float result=a+b;uint x=NB(a),y=NB(b),mx=x&0x7fffffffu,my=y&0x7fffffffu;
bool cancel=mx==my&&(((x^y)&0x80000000u)!=0u||mx==0u);return nfinish(result,!cancel,diagnostic,l,c,p);}
float nsub(float a,float b,inout NDIAG diagnostic,uint l,uint c,uint p){precise float result=a-b;uint x=NB(a),y=NB(b);return nfinish(result,!(x==y||((x&0x7fffffffu)==0u&&(y&0x7fffffffu)==0u)),diagnostic,l,c,p);}
float nmul(float a,float b,inout NDIAG diagnostic,uint l,uint c,uint p){precise float result=a*b;return nfinish(result,(NB(a)&0x7fffffffu)!=0u&&(NB(b)&0x7fffffffu)!=0u,diagnostic,l,c,p);}
float ndiv(float a,float b,inout NDIAG diagnostic,uint l,uint c,uint p){bool valid=(NB(b)&0x7fffffffu)!=0u;precise float safeA=valid?a:NF(0u),safeB=valid?b:NF(0x3f800000u);precise float result=safeA/safeB;
if(!valid){bad(diagnostic,2u,l,c,p);return NF(0u);}return nfinish(result,(NB(a)&0x7fffffffu)!=0u,diagnostic,l,c,p);}
float npow(float a,float b,inout NDIAG diagnostic,uint l,uint c,uint p){bool inrange=b>=NF(0xc2800000u)&&b<=NF(0x42800000u);precise float safeExponent=inrange?b:NF(0u);int degree=int(safeExponent);
if(!inrange||float(degree)!=b){bad(diagnostic,4u,l,c,p);return NF(0u);}uint exponent=uint(degree<0?-degree:degree);precise float result=NF(0x3f800000u),base=a;
if(degree<0){base=ndiv(result,base,diagnostic,l,c,p);if(diagnostic.x!=0u)return NF(0u);}
for(uint bit=0u;bit<7u;++bit){if((exponent&1u)!=0u){result=nmul(result,base,diagnostic,l,c,p);if(diagnostic.x!=0u)return NF(0u);}exponent>>=1u;
if(exponent!=0u){base=nmul(base,base,diagnostic,l,c,p);if(diagnostic.x!=0u)return NF(0u);}}return result;}
// Convert the uint64 nanosecond COUNT with one integer-defined binary32 RNE.
// Subsequent seconds scaling uses native GPU division precision; it does not
// promise a CPU-identical last-bit result for the same count.
uint nclock(uint low,uint high){uint result=0u;if((low|high)!=0u){uint top=high!=0u?32u+uint(NFIRST(high)):uint(NFIRST(low));uint significand=0u;
if(top<=23u)significand=low<<(23u-top);else{uint shift=top-23u,guard=0u,sticky=0u;
if(shift<32u){significand=(high<<(32u-shift))|(low>>shift);guard=(low>>(shift-1u))&1u;sticky=low&((1u<<(shift-1u))-1u);}
else if(shift==32u){significand=high;guard=low>>31u;sticky=low&0x7fffffffu;}
else{uint upperShift=shift-32u;significand=high>>upperShift;guard=(high>>(upperShift-1u))&1u;sticky=(high&((1u<<(upperShift-1u))-1u))|low;}
if(guard!=0u&&(sticky!=0u||(significand&1u)!=0u))++significand;if(significand==0x1000000u){significand>>=1u;++top;}}
result=((top+127u)<<23u)|(significand&0x7fffffu);}return result;}
)";
    if(!geometry.rows.empty()){out<<"static const uint4 nativeGeometry["<<geometry.rows.size()<<"]={\n";
        for(std::size_t i=0;i<geometry.rows.size();++i){if(i)out<<",\n";const auto& row=geometry.rows[i];out<<"uint4("<<hex(row[0])<<","<<hex(row[1])<<","<<hex(row[2])<<","<<hex(row[3])<<")";}out<<"\n};\n";}
    out<<(glsl?"layout(local_size_x=64,local_size_y=1,local_size_z=1)in;void main(){uint index=gl_GlobalInvocationID.x;\n":"[numthreads(64,1,1)]void CSMain(uint3 thread:SV_DispatchThreadID){uint index=thread.x;\n");
    const auto states=static_cast<std::uint32_t>(program.ir.states.size()),vertices=program.ir.vertices,faces=program.ir.faces;
    const auto overwritten=action==NativeAction::reset?reset_buffer_coverage(graph,{states,vertices,faces}):std::array<bool,3>{};
    out<<"uint count=parameters[0].x;if(index>=count)return;uint bank=parameters[0].y,old=parameters[0].z,s=index*4u;NDIAG diagnostic=NDIAG(0u,0u,0u,0u);\n";
    out<<"uint state=(bank*count+index)*"<<hex(states)<<",oldState=(old*count+index)*"<<hex(states)<<";uint vertex=(bank*count+index)*"<<hex(vertices*3)<<",oldVertex=(old*count+index)*"<<hex(vertices*3)<<";\n";
    out<<"uint face=(bank*count+index)*"<<hex(faces*3)<<",oldFace=(old*count+index)*"<<hex(faces*3)<<",frame=(bank*count+index)*10u;\n";
    if(real){out<<"precise float nativeDt=NF(0u);\n";if(action==NativeAction::step)out<<
        "if(parameters[0].w==1u){uint rawCount=nclock(r6(index*2u),r6(index*2u+1u));nativeDt=ndiv(NF(rawCount),NF(0x4e6e6b28u),diagnostic,0u,0u,0u);}\n"
        "else nativeDt=nload(r5(index),diagnostic,0u,0u,0u);if(diagnostic.x!=0u){commit_error(s,diagnostic);return;}if(nativeDt<NF(0u)){bad(diagnostic,8u,0u,0u,0u);commit_error(s,diagnostic);return;}\n";}
    else if(action==NativeAction::step)out<<"if(NI(r5(index))<0){bad(diagnostic,8u,0u,0u,0u);commit_error(s,diagnostic);return;}\n";
    for(unsigned buffer=0;buffer<3;++buffer){const auto length=buffer==0?states:buffer==1?vertices*3:faces*3;const std::array<const char*,3> base{"state","vertex","face"},prior{"oldState","oldVertex","oldFace"};
        // RESET terminals always write their fixed slot. No expression reads
        // the work bank: load_state reads the separate immutable old bank.
        // A full mask proves this fill dead even when an operation fails,
        // because the runner never commits a bank with a nonzero diagnostic.
        // Partial/manual graphs and every nonreset copy retain their fill.
        if(length==0||overwritten[buffer])continue;const auto counter="copy"+std::to_string(buffer);
        out<<"for(uint "<<counter<<"=0u;"<<counter<<"<"<<hex(length)<<";++"<<counter<<")w"<<buffer<<"("<<base[buffer]<<"+"<<counter<<","<<(action==NativeAction::reset?"0u":"r"+std::to_string(buffer)+"("+prior[buffer]+"+"+counter+")")<<");\n";}
    const auto one=real?"0x3f800000u":"1u";
    // The header is not consumed during an action. Keep its final raw values
    // local, preserving terminal validation order, then store each word once.
    // This also avoids overlapping byte-address stores during DXBC lowering.
    out<<"uint nativeRotation0=0u,nativeRotation1=0u,nativeRotation2=0u,nativeColor0="<<one<<",nativeColor1="<<one<<",nativeColor2="<<one<<",nativeDraw=0u;\n";
    const auto type=real?"precise float ":"int ",from=real?"NF":"NI",to=real?"NB":"NU";
    std::vector<std::optional<std::uint32_t>> known(graph.value_count);
    const auto raw_value=[&](std::uint32_t value){return known.at(value)?hex(*known.at(value)):std::string(to)+"("+id(value)+")";};
    for(std::size_t at=0;at<graph.instructions.size();++at){const auto& node=graph.instructions[at];const auto a=id(node.arguments[0]),b=id(node.arguments[1]);
        const auto location=",diagnostic,"+hex(node.line)+","+hex(node.column)+","+hex(static_cast<std::uint32_t>(at));
        switch(node.opcode){case NativeOpcode::constant:
            // Prove literal encodings once while generating source, instead
            // of building thousands of impossible diagnostic branches in FXC
            // and driver compilers. Invalid manually supplied literals retain
            // their checked load and original first-error source location.
            if(real&&real_encoding(node.immediate)!=NativeError::none)out<<type<<id(node.result)<<"=nload("<<hex(node.immediate)<<location<<");\n";
            else{known.at(node.result)=node.immediate;out<<type<<id(node.result)<<"="<<from<<"("<<hex(node.immediate)<<");\n";}break;
        case NativeOpcode::load_state:if(real)out<<type<<id(node.result)<<"=nload(r0(oldState+"<<hex(node.immediate)<<")"<<location<<");\n";
            else out<<type<<id(node.result)<<"=NI(r0(oldState+"<<hex(node.immediate)<<"));\n";break;
        case NativeOpcode::load_dt:out<<type<<id(node.result)<<"="<<(real?"nativeDt":"NI(r5(index))")<<";\n";break;
        case NativeOpcode::positive:known.at(node.result)=known.at(node.arguments[0]);out<<type<<id(node.result)<<"="<<a<<";\n";break;
        case NativeOpcode::negate:out<<type<<id(node.result)<<"=nneg("<<a<<location<<");\n";break;
        case NativeOpcode::add:case NativeOpcode::subtract:case NativeOpcode::multiply:case NativeOpcode::divide:case NativeOpcode::power:{const char* name=node.opcode==NativeOpcode::add?"nadd":node.opcode==NativeOpcode::subtract?"nsub":node.opcode==NativeOpcode::multiply?"nmul":node.opcode==NativeOpcode::divide?"ndiv":"npow";
            out<<type<<id(node.result)<<"="<<name<<"("<<a<<","<<b<<location<<");\n";break;}
        case NativeOpcode::store_state:out<<"w0(state+"<<hex(node.immediate)<<","<<raw_value(node.arguments[0])<<");\n";break;
        case NativeOpcode::vertex:case NativeOpcode::face:case NativeOpcode::rotation:case NativeOpcode::color:{
            // Only this store moves. Every graph node retains its ordinal and
            // every literal/arithmetic diagnostic is still emitted in place.
            if((node.opcode==NativeOpcode::vertex&&geometry.counts[0])||(node.opcode==NativeOpcode::face&&geometry.counts[1]))break;
            std::array<std::string,3> indices;std::array<std::uint32_t,3> fixed_indices{};
            bool fixed_valid=node.opcode==NativeOpcode::face||node.opcode==NativeOpcode::color;
            for(unsigned axis=0;fixed_valid&&axis<3;++axis){const auto bits=known.at(node.arguments[axis]);if(!bits){fixed_valid=false;break;}
                if(node.opcode==NativeOpcode::face){const auto index=real?constant_real_index(*bits,vertices):(*bits<vertices?std::optional(*bits):std::nullopt);
                    if(index)fixed_indices[axis]=*index;else fixed_valid=false;
                }else if(real){const auto magnitude=*bits&0x7fffffffu;fixed_valid=magnitude<=0x3f800000u&&(!(*bits&0x80000000u)||magnitude==0u);}
                else fixed_valid=*bits<=1u;}
            if(real&&node.opcode==NativeOpcode::face&&!fixed_valid){for(unsigned axis=0;axis<3;++axis){const auto value=id(node.arguments[axis]),suffix=std::to_string(at)+"_"+std::to_string(axis);indices[axis]="faceIndex"+suffix;
                    // Sanitize before conversion, including on implementations
                    // which evaluate both sides of a source select eagerly.
                    out<<"precise float faceSafe"<<suffix<<"=("<<value<<">=NF(0u)&&"<<value<<"<float("<<hex(vertices)<<"))?"<<value<<":NF(0u);uint "<<indices[axis]<<"=uint(faceSafe"<<suffix<<");\n";}
                out<<"if(";for(unsigned axis=0;axis<3;++axis){if(axis)out<<"||";const auto value=id(node.arguments[axis]);out<<value<<"<NF(0u)||"<<value<<">=float("<<hex(vertices)<<")||float("<<indices[axis]<<")!="<<value;}
                out<<"){bad(diagnostic,5u,"<<hex(node.line)<<","<<hex(node.column)<<","<<hex(static_cast<std::uint32_t>(at))<<");}\n";
            }else if((node.opcode==NativeOpcode::face||node.opcode==NativeOpcode::color)&&!fixed_valid){out<<"if(";for(unsigned axis=0;axis<3;++axis){if(axis)out<<"||";const auto value=id(node.arguments[axis]);
                    if(real)out<<value<<"<NF(0u)||"<<value<<">NF(0x3f800000u)";
                    else out<<value<<"<0||"<<value<<(node.opcode==NativeOpcode::face?">=NI(":">NI(")<<hex(node.opcode==NativeOpcode::face?vertices:1u)<<")";}
                out<<"){bad(diagnostic,"<<(node.opcode==NativeOpcode::face?5:6)<<"u,"<<hex(node.line)<<","<<hex(node.column)<<","<<hex(static_cast<std::uint32_t>(at))<<");}\n";}
            if(node.opcode==NativeOpcode::rotation||node.opcode==NativeOpcode::color){const auto local=node.opcode==NativeOpcode::rotation?"nativeRotation":"nativeColor";
                for(unsigned axis=0;axis<3;++axis)out<<local<<axis<<"="<<raw_value(node.arguments[axis])<<";\n";
            }else{const auto buffer=node.opcode==NativeOpcode::vertex?1:2;const auto start=(node.opcode==NativeOpcode::vertex?"vertex+":"face+")+hex(node.immediate*3);
                std::array<std::string,3> words;for(unsigned axis=0;axis<3;++axis){
                    if(node.opcode==NativeOpcode::face&&fixed_valid)words[axis]=hex(fixed_indices[axis]);else if(real&&node.opcode==NativeOpcode::face)words[axis]=indices[axis];else words[axis]=raw_value(node.arguments[axis]);}
                // Store3 requires only four-byte alignment, including for a
                // three-word stride. The preceding terminal checks and the
                // three raw payload words are unchanged; the header stays
                // on its separate final scalar-store path.
                // https://learn.microsoft.com/windows/win32/direct3dhlsl/sm5-object-rwbyteaddressbuffer-store3
                if(!glsl)out<<"B"<<buffer<<".Store3(("<<start<<")*4u,uint3("<<words[0]<<","<<words[1]<<","<<words[2]<<"));\n";
                else for(unsigned axis=0;axis<3;++axis)out<<"w"<<buffer<<"("<<start<<"+"<<axis<<"u,"<<words[axis]<<");\n";}break;}
        case NativeOpcode::draw:if(vertices<3||faces==0)out<<"bad(diagnostic,7u,"<<hex(node.line)<<","<<hex(node.column)<<","<<hex(static_cast<std::uint32_t>(at))<<");\n";else out<<"nativeDraw=1u;\n";break;}
    }
    // Geometry is never read from scratch within an action. Constant stores
    // may therefore be delayed, preserving duplicate order within each buffer.
    // The normal ABI assigns the two geometry buffers separate allocations.
    if(geometry.counts[0])out<<"[loop]for(uint nativeVertexRow=0u;nativeVertexRow<"<<hex(geometry.counts[0])<<";++nativeVertexRow){uint4 nativeVertexData=nativeGeometry[nativeVertexRow];B1.Store3((vertex+nativeVertexData.x)*4u,nativeVertexData.yzw);}\n";
    if(geometry.counts[1])out<<"[loop]for(uint nativeFaceRow="<<hex(geometry.counts[0])<<";nativeFaceRow<"<<hex(static_cast<std::uint32_t>(geometry.rows.size()))<<";++nativeFaceRow){uint4 nativeFaceData=nativeGeometry[nativeFaceRow];B2.Store3((face+nativeFaceData.x)*4u,nativeFaceData.yzw);}\n";
    // Numeric helpers sanitize their outputs; addresses come from validated
    // static IR metadata. Continue straight-line work after a local fault,
    // retaining its first location, instead of creating a return CFG per node.
    // The runner rejects the entire work bank if any item's status is nonzero.
    out<<"if(diagnostic.x==0u){\n";
    for(unsigned axis=0;axis<3;++axis)out<<"w3(frame+"<<axis<<"u,nativeRotation"<<axis<<");\n";
    for(unsigned axis=0;axis<3;++axis)out<<"w3(frame+"<<axis+3<<"u,nativeColor"<<axis<<");\n";
    out<<"w3(frame+6u,"<<hex(vertices)<<");w3(frame+7u,"<<hex(faces)<<");w3(frame+8u,nativeDraw);w3(frame+9u,0u);\n}\ncommit_error(s,diagnostic);\n}\n";return out.str();}
} //namespace

std::int32_t native_parse_integer(std::string_view text,Notation notation){notation_valid(notation);bool negative=false;if(!text.empty()&&(text.front()=='+'||text.front()=='-')){negative=text.front()=='-';text.remove_prefix(1);}const auto value=magnitude(text,notation);
    if(!negative&&value>static_cast<std::uint32_t>(maximum))throw std::invalid_argument("El literal positivo supera INT_MAX.");const auto bits=negative?0u-value:value;return std::bit_cast<std::int32_t>(bits);}
std::uint32_t native_parse_binary32(std::string_view text,Notation notation){notation_valid(notation);bool negative=false;if(!text.empty()&&(text.front()=='+'||text.front()=='-')){negative=text.front()=='-';text.remove_prefix(1);}
    if(text.empty())throw std::invalid_argument("Faltan cifras base12.");if(text.size()==1&&text.front()==notation.twelve_alias)return rational_binary32(Big(12),Big(1),negative);
    Big numerator,denominator(1);bool point=false,seen=false,tail=false;unsigned fraction_count=0,whole_digits=0;
    for(char c:text){if(c=='.'){if(point)throw std::invalid_argument("El literal contiene dos puntos fraccionarios.");point=true;continue;}
        const int digit=c>='0'&&c<='9'?c-'0':c==notation.ten?10:c==notation.eleven?11:-1;if(digit<0)throw std::invalid_argument("Cifra base12 inválida o alias no independiente.");seen=true;
        if(point){if(fraction_count<75){numerator.multiply(12);numerator.add(static_cast<unsigned>(digit));denominator.multiply(12);++fraction_count;}else tail=tail||digit!=0;}
        else{if(digit!=0||whole_digits)++whole_digits;if(whole_digits>36)throw std::invalid_argument("El literal supera el rango finito binary32.");numerator.multiply(12);numerator.add(static_cast<unsigned>(digit));}}
    if(!seen)throw std::invalid_argument("El punto necesita al menos una cifra.");
    // Every binary32 rounding midpoint is an integer multiple of2^-150, hence
    // also of12^-75 (12^75=2^150*3^75). Beyond75 fractional radix12 digits,
    // an exact sticky bit resolves a midpoint; no rounding boundary can be
    // crossed otherwise. This bound keeps arbitrary-length source parsing
    // exact without allocating an arbitrary-precision runtime representation.
    return rational_binary32(numerator,denominator,negative,tail);}
// CPU reference, not the runtime clock path: exact RNE of the integer count,
// followed by host binary32 division. API-native GPU division is a separate
// precision contract and need not reproduce this oracle's final low bits.
std::uint32_t native_elapsed_binary32(std::uint64_t nanoseconds){const auto count=rational_binary32(Big(nanoseconds),Big(1),false);const auto seconds=real_calculate(NativeOpcode::divide,count,0x4e6e6b28u);
    if(seconds.error!=NativeError::none)throw std::logic_error("La conversión temporal binary32 quedó fuera de su dominio.");return seconds.bits;}
NativeProgram native_compile(std::string_view source,Notation notation,NativeScalarProfile profile){if(profile!=NativeScalarProfile::ordinary_i32&&profile!=NativeScalarProfile::ordinary_binary32)throw std::invalid_argument("Perfil escalar nativo desconocido.");
    NativeProgram result;result.scalarProfile=profile;result.ir=compile_ir(source,notation,false);
    for(unsigned action=0;action<3;++action)result.actions[action]=Builder(result.ir,static_cast<NativeAction>(action),profile).finish();
    std::string identity=profile==NativeScalarProfile::ordinary_i32?"SMO_ORDINARY_I32_V1\n":"SMO_ORDINARY_BINARY32_NORMAL_V1\n";identity.append({notation.zero,notation.ten,notation.eleven,notation.twelve_alias});identity+='\n';identity+=source;result.identity=sha256(identity);return result;}
std::string native_hlsl(const NativeProgram& program,NativeAction action){return shaders(program,action,false);}
std::string native_glsl(const NativeProgram& program,NativeAction action){return shaders(program,action,true);}
NativeExecution native_reference(const NativeProgram& program,NativeAction action,const NativeFrame& previous,std::int32_t dt){if(program.scalarProfile!=NativeScalarProfile::ordinary_i32)throw std::invalid_argument("El oráculo int32 no puede reinterpretar un programa binary32.");if(static_cast<unsigned>(action)>2)throw std::invalid_argument("Acción nativa inexistente.");
    NativeFrame work=previous;const auto& ir=program.ir;if(action==NativeAction::reset){work.states.assign(ir.states.size(),0);work.vertices.assign(ir.vertices,{});work.faces.assign(ir.faces,{});}
    else if(work.states.size()!=ir.states.size()||work.vertices.size()!=ir.vertices||work.faces.size()!=ir.faces)throw std::invalid_argument("El estado anterior no corresponde al programa nativo.");
    work.rotation={};work.color={1,1,1};work.draw_requested=false;if(action==NativeAction::step&&dt<0)return {previous,{NativeError::time,0,0,0}};
    const auto& graph=program.actions[static_cast<unsigned>(action)];std::vector<std::int32_t> values(graph.value_count,0);
    for(std::size_t at=0;at<graph.instructions.size();++at){const auto& node=graph.instructions[at];NativeError error=NativeError::none;const auto argument=[&](unsigned i){return values.at(node.arguments[i]);};
        switch(node.opcode){case NativeOpcode::constant:values.at(node.result)=std::bit_cast<std::int32_t>(node.immediate);break;
        case NativeOpcode::load_state:values.at(node.result)=previous.states.at(node.immediate);break;
        case NativeOpcode::load_dt:values.at(node.result)=dt;break;
        case NativeOpcode::positive:case NativeOpcode::negate:case NativeOpcode::add:case NativeOpcode::subtract:case NativeOpcode::multiply:case NativeOpcode::divide:case NativeOpcode::power:{
            const auto calculated=calculate(node.opcode,argument(0),node.arguments[1]==native_no_value?0:argument(1));error=calculated.error;values.at(node.result)=calculated.value;break;}
        case NativeOpcode::store_state:work.states.at(node.immediate)=argument(0);break;
        case NativeOpcode::vertex:work.vertices.at(node.immediate)={argument(0),argument(1),argument(2)};break;
        case NativeOpcode::face:for(unsigned axis=0;axis<3;++axis){const auto value=argument(axis);if(value<0||static_cast<std::uint32_t>(value)>=ir.vertices){error=NativeError::index;break;}work.faces.at(node.immediate)[axis]=static_cast<std::uint32_t>(value);}break;
        case NativeOpcode::rotation:work.rotation={argument(0),argument(1),argument(2)};break;
        case NativeOpcode::color:for(unsigned axis=0;axis<3;++axis){const auto value=argument(axis);if(value<0||value>1){error=NativeError::color;break;}work.color[axis]=value;}break;
        case NativeOpcode::draw:if(ir.vertices<3||ir.faces==0)error=NativeError::geometry;else work.draw_requested=true;break;}
        if(error!=NativeError::none)return {previous,{error,node.line,node.column,static_cast<std::uint32_t>(at)}};
    }return {std::move(work),{}};}
NativeRealExecution native_reference_real(const NativeProgram& program,NativeAction action,const NativeRealFrame32& previous,std::uint32_t dt_bits){if(program.scalarProfile!=NativeScalarProfile::ordinary_binary32)throw std::invalid_argument("El oráculo binary32 requiere su perfil explícito.");
    if(static_cast<unsigned>(action)>2)throw std::invalid_argument("Acción nativa inexistente.");NativeRealFrame32 work=previous;const auto& ir=program.ir;
    if(action==NativeAction::reset){work.states.assign(ir.states.size(),0);work.vertices.assign(ir.vertices,{});work.faces.assign(ir.faces,{});}
    else if(work.states.size()!=ir.states.size()||work.vertices.size()!=ir.vertices||work.faces.size()!=ir.faces)throw std::invalid_argument("El estado anterior no corresponde al programa binary32.");
    work.rotation={};work.color={0x3f800000u,0x3f800000u,0x3f800000u};work.draw_requested=false;
    if(action==NativeAction::step){auto error=real_encoding(dt_bits);if(error==NativeError::none&&(dt_bits&0x80000000u)&&(dt_bits&0x7fffffffu))error=NativeError::time;if(error!=NativeError::none)return {previous,{error,0,0,0}};}
    const auto& graph=program.actions[static_cast<unsigned>(action)];std::vector<std::uint32_t> values(graph.value_count,0);
    for(std::size_t at=0;at<graph.instructions.size();++at){const auto& node=graph.instructions[at];NativeError error=NativeError::none;const auto raw=[&](unsigned i){return values.at(node.arguments[i]);};const auto value=[&](unsigned i){return std::bit_cast<float>(raw(i));};
        switch(node.opcode){case NativeOpcode::constant:values.at(node.result)=node.immediate;error=real_encoding(node.immediate);break;
        case NativeOpcode::load_state:values.at(node.result)=previous.states.at(node.immediate);error=real_encoding(values.at(node.result));break;
        case NativeOpcode::load_dt:values.at(node.result)=dt_bits;break;
        case NativeOpcode::positive:case NativeOpcode::negate:case NativeOpcode::add:case NativeOpcode::subtract:case NativeOpcode::multiply:case NativeOpcode::divide:case NativeOpcode::power:{const auto result=real_calculate(node.opcode,raw(0),node.arguments[1]==native_no_value?0u:raw(1));values.at(node.result)=result.bits;error=result.error;break;}
        case NativeOpcode::store_state:work.states.at(node.immediate)=raw(0);break;
        case NativeOpcode::vertex:work.vertices.at(node.immediate)={raw(0),raw(1),raw(2)};break;
        case NativeOpcode::face:for(unsigned axis=0;axis<3;++axis){const auto coordinate=value(axis);if(coordinate<0.0f||coordinate>=static_cast<float>(ir.vertices)){error=NativeError::index;break;}
            const auto index=static_cast<std::uint32_t>(coordinate);if(static_cast<float>(index)!=coordinate){error=NativeError::index;break;}work.faces.at(node.immediate)[axis]=index;}break;
        case NativeOpcode::rotation:work.rotation={raw(0),raw(1),raw(2)};break;
        case NativeOpcode::color:for(unsigned axis=0;axis<3;++axis){const auto component=value(axis);if(component<0.0f||component>1.0f){error=NativeError::color;break;}work.color[axis]=raw(axis);}break;
        case NativeOpcode::draw:if(ir.vertices<3||ir.faces==0)error=NativeError::geometry;else work.draw_requested=true;break;}
        if(error!=NativeError::none)return {previous,{error,node.line,node.column,static_cast<std::uint32_t>(at)}};
    }return {std::move(work),{}};}
} //namespace lgaed::smo
