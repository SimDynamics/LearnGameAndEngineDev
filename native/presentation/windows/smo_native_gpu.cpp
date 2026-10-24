#include "presentation/windows/smo_native_gpu.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace lgaed::presentation { namespace {
using U=std::uint32_t;using Op=smo::NativeOpcode;using Action=smo::NativeAction;using Error=smo::NativeError;
void canceled(std::stop_token stop){if(stop.stop_requested())throw std::runtime_error("Compilador nativo SMO cancelado.");}
unsigned action_index(Action action){const auto index=static_cast<unsigned>(action);if(index>2)throw std::invalid_argument("Acción nativa desconocida.");return index;}
unsigned arity(Op op){switch(op){case Op::constant:case Op::load_state:case Op::load_dt:case Op::draw:return 0;
    case Op::positive:case Op::negate:case Op::store_state:return 1;case Op::add:case Op::subtract:case Op::multiply:case Op::divide:case Op::power:return 2;
    case Op::vertex:case Op::face:case Op::rotation:case Op::color:return 3;}throw std::invalid_argument("Operación SSA nativa desconocida.");}
bool terminal(Op op){return op==Op::store_state||op==Op::vertex||op==Op::face||op==Op::rotation||op==Op::color||op==Op::draw;}
void validate(const smo::NativeProgram& program){if(program.scalarProfile!=smo::NativeScalarProfile::ordinary_i32&&program.scalarProfile!=smo::NativeScalarProfile::ordinary_binary32)throw std::invalid_argument("Perfil escalar nativo desconocido.");if(program.ir.states.size()>65536||program.ir.vertices>1024||program.ir.faces>2048)throw std::invalid_argument("Metadatos del programa nativo excesivos.");
    for(const auto& graph:program.actions){if(graph.value_count>262144||graph.instructions.size()>262144)throw std::invalid_argument("Grafo SSA nativo excesivo.");std::vector<bool> defined(graph.value_count);
        for(const auto& node:graph.instructions){const auto count=arity(node.opcode);for(unsigned a=0;a<count;++a)if(node.arguments[a]>=defined.size()||!defined[node.arguments[a]])throw std::invalid_argument("Uso SSA anterior a su definición.");
            if(!terminal(node.opcode)){if(node.result>=defined.size()||defined[node.result])throw std::invalid_argument("Definición SSA inválida o repetida.");defined[node.result]=true;}
            if((node.opcode==Op::load_state||node.opcode==Op::store_state)&&node.immediate>=program.ir.states.size())throw std::invalid_argument("Estado SSA fuera del programa.");
            if(node.opcode==Op::vertex&&node.immediate>=program.ir.vertices)throw std::invalid_argument("Vértice SSA fuera del programa.");
            if(node.opcode==Op::face&&node.immediate>=program.ir.faces)throw std::invalid_argument("Cara SSA fuera del programa.");}}
}

// A direct SPIR-V1.3 emitter for the explicit checked int32/binary32 profiles.
// The graph is compiler input, never an opcode buffer consumed by the GPU.
// Numeric opcodes/enumerants follow the official SPIR-V specification:
// https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html
// Vulkan consumes these words directly via vkCreateShaderModule; no external
// GLSL compiler, SDK discovery, temporary file or process participates.
class Spirv {
    using Words=std::vector<U>;
    Words annotations_,types_,code_;
    U next_=1,void_,bool_,uint_,int_,uint3_,function_,mul_type_,storage_uint_,push_uint_,gid_,params_,main_,block_=0,float_=0,extension_=0;
    std::array<U,8> buffers_{};
    std::map<U,U> constants_,signed_,floating_;
    U item_=0,count_=0,work_=0,old_=0,dt_=0,error_=0,line_=0,column_=0,instruction_=0;
    std::array<U,4> destination_{},previous_{};
    std::array<U,smo::native_frame_words> frame_values_{};
    const smo::NativeProgram& program_;
    const smo::NativeGraph& graph_;
    Action action_;
    static void emit(Words& target,U opcode,std::initializer_list<U> operands){target.push_back((static_cast<U>(operands.size()+1u)<<16u)|opcode);target.insert(target.end(),operands);}
    static void emit(Words& target,U opcode,const Words& operands){if(operands.size()>65534)throw std::runtime_error("Instrucción SPIR-V excesiva.");target.push_back((static_cast<U>(operands.size()+1u)<<16u)|opcode);target.insert(target.end(),operands.begin(),operands.end());}
    static Words text(std::string_view value){Words words((value.size()+4u)/4u);for(std::size_t i=0;i<value.size();++i)words[i/4u]|=static_cast<U>(static_cast<unsigned char>(value[i]))<<((i%4u)*8u);return words;}
    U id(){if(next_==0xffffffffu)throw std::runtime_error("IDs SPIR-V agotados.");return next_++;}
    U constant(U bits){if(const auto found=constants_.find(bits);found!=constants_.end())return found->second;const auto value=id();emit(types_,43,{uint_,value,bits});constants_[bits]=value;return value;}
    U value(U opcode,U type,std::initializer_list<U> args){const auto result=id();Words operands{type,result};operands.insert(operands.end(),args);emit(code_,opcode,operands);return result;}
    U binary(U opcode,U a,U b){return value(opcode,uint_,{a,b});}
    U boolean(U opcode,U a,U b){return value(opcode,bool_,{a,b});}
    U equal(U a,U b){return boolean(170,a,b);}U unequal(U a,U b){return boolean(171,a,b);}
    U both(U a,U b){return boolean(167,a,b);}U either(U a,U b){return boolean(166,a,b);}
    U choose(U condition,U yes,U no){return value(169,uint_,{condition,yes,no});}
    U as_signed(U bits){if(const auto found=signed_.find(bits);found!=signed_.end())return found->second;const auto result=value(124,int_,{bits});signed_[bits]=result;return result;}
    bool real()const{return program_.scalarProfile==smo::NativeScalarProfile::ordinary_binary32;}
    U as_float(U bits){if(const auto found=floating_.find(bits);found!=floating_.end())return found->second;const auto result=value(124,float_,{bits});floating_[bits]=result;return result;}
    U float_compare(U opcode,U a,U b){return boolean(opcode,as_float(a),as_float(b));}
    U float_op(U opcode,U a,U b){const auto result=value(opcode,float_,{as_float(a),as_float(b)});emit(annotations_,71,{result,42});return value(124,uint_,{result});}
    U float_zero(U bits){return equal(binary(199,bits,constant(0x7fffffffu)),constant(0));}
    U logical_not(U condition){return value(168,bool_,{condition});}
    void classify(U bits,const smo::NativeInstruction& node,U index,U enabled=0){const auto magnitude=binary(199,bits,constant(0x7fffffffu));
        auto nonfinite=boolean(174,magnitude,constant(0x7f800000u));auto underflow=both(unequal(magnitude,constant(0)),boolean(176,magnitude,constant(0x00800000u)));
        if(enabled){nonfinite=both(enabled,nonfinite);underflow=both(enabled,underflow);}fault(nonfinite,Error::nonfinite,node,index);fault(underflow,Error::underflow,node,index);}
    struct RealCalculation {U bits,nonfinite,underflow;};
    RealCalculation real_calculate(U opcode,U a,U b){const auto result=float_op(opcode,a,b),magnitude=binary(199,result,constant(0x7fffffffu));
        const auto nonfinite=boolean(174,magnitude,constant(0x7f800000u));auto underflow=both(unequal(magnitude,constant(0)),boolean(176,magnitude,constant(0x00800000u)));U nonzero=0;
        if(opcode==129)nonzero=float_compare(182,a,binary(198,b,constant(0x80000000u)));
        else if(opcode==131)nonzero=float_compare(182,a,b);
        else if(opcode==133)nonzero=both(logical_not(float_zero(a)),logical_not(float_zero(b)));
        else nonzero=logical_not(float_zero(a));
        underflow=either(underflow,both(float_zero(result),nonzero));return {result,nonfinite,underflow};}
    U real_result(const RealCalculation& result,const smo::NativeInstruction& node,U index,U enabled=0){auto nonfinite=result.nonfinite,underflow=result.underflow;
        if(enabled){nonfinite=both(enabled,nonfinite);underflow=both(enabled,underflow);}fault(nonfinite,Error::nonfinite,node,index);fault(underflow,Error::underflow,node,index);return result.bits;}
    U real_divide(U a,U b,const smo::NativeInstruction& node,U index){const auto zero=float_zero(b);fault(zero,Error::division_zero,node,index);
        const auto safe_a=choose(zero,constant(0),a),safe_b=choose(zero,constant(0x3f800000u),b);return real_result(real_calculate(136,safe_a,safe_b),node,index);}
    U real_power(U a,U b,const smo::NativeInstruction& node,U index){const auto range=both(float_compare(190,b,constant(0xc2800000u)),float_compare(188,b,constant(0x42800000u)));
        const auto safe=choose(range,b,constant(0));const auto signed_exponent=value(110,int_,{as_float(safe)}),roundtrip=value(111,float_,{signed_exponent});
        const auto integral=boolean(180,as_float(b),roundtrip),valid=both(range,integral);fault(logical_not(valid),Error::exponent,node,index);
        const auto signed_bits=value(124,uint_,{signed_exponent}),negative=signed_less(signed_bits,constant(0));
        const auto exponent=choose(valid,choose(negative,sub(constant(0),signed_bits),signed_bits),constant(0));
        const auto reciprocal=real_divide(constant(0x3f800000u),choose(negative,a,constant(0x3f800000u)),node,index);auto factor=choose(negative,reciprocal,a),result=constant(0x3f800000u);
        for(U bit=0;bit<7;++bit){const auto remaining=binary(194,exponent,constant(bit)),use=unequal(binary(199,remaining,constant(1)),constant(0));
            const auto next=real_result(real_calculate(133,result,factor),node,index,use);result=choose(use,next,result);
            if(bit<6){const auto needed=boolean(172,remaining,constant(1));const auto square=real_result(real_calculate(133,factor,factor),node,index,needed);factor=choose(needed,square,factor);}}
        return result;}
    U elapsed(){const auto base=mul(item_,constant(2)),low=load(6,base),high=load(6,at(base,constant(1)));
        const auto high_nonzero=unequal(high,constant(0)),nonzero=either(high_nonzero,unequal(low,constant(0)));
        const auto high_msb=value(124,uint_,{value(12,int_,{extension_,75,high})}),low_msb=value(124,uint_,{value(12,int_,{extension_,75,low})});
        const auto top=choose(nonzero,choose(high_nonzero,add(high_msb,constant(32)),low_msb),constant(0)),discard=boolean(172,top,constant(23));
        const auto shift=choose(discard,sub(top,constant(23)),constant(1)),small=boolean(176,shift,constant(32));
        const auto small_shift=choose(small,shift,constant(1)),large_shift=choose(small,constant(0),sub(shift,constant(32)));
        const auto small_part=binary(197,binary(194,low,small_shift),binary(196,high,sub(constant(32),small_shift))),large_part=binary(194,high,large_shift);
        auto significand=choose(small,small_part,large_part);const auto guard_low=boolean(178,shift,constant(32));
        const auto low_bit=choose(guard_low,sub(shift,constant(1)),constant(0)),high_bit=choose(guard_low,constant(0),sub(shift,constant(33)));
        const auto guard=unequal(binary(199,choose(guard_low,binary(194,low,low_bit),binary(194,high,high_bit)),constant(1)),constant(0));
        const auto low_mask=sub(binary(196,constant(1),low_bit),constant(1)),high_mask=sub(binary(196,constant(1),high_bit),constant(1));
        const auto sticky_low=unequal(binary(199,low,low_mask),constant(0)),sticky_high=either(unequal(low,constant(0)),unequal(binary(199,high,high_mask),constant(0)));
        const auto sticky=choose(guard_low,choose(sticky_low,constant(1),constant(0)),choose(sticky_high,constant(1),constant(0)));
        const auto odd=unequal(binary(199,significand,constant(1)),constant(0)),increment=both(guard,either(unequal(sticky,constant(0)),odd));
        significand=add(significand,choose(increment,constant(1),constant(0)));const auto carry=equal(significand,constant(0x01000000u));
        const auto rounded=choose(carry,binary(194,significand,constant(1)),significand),exponent=add(add(top,constant(127)),choose(carry,constant(1),constant(0)));
        const auto rounded_bits=binary(197,binary(196,exponent,constant(23)),binary(199,rounded,constant(0x007fffffu)));
        const auto left_shift=choose(discard,constant(0),sub(constant(23),top)),exact_bits=binary(197,binary(196,add(top,constant(127)),constant(23)),binary(199,binary(196,low,left_shift),constant(0x007fffffu)));
        const auto magnitude=choose(nonzero,choose(discard,rounded_bits,exact_bits),constant(0));return float_op(136,magnitude,constant(0x4e6e6b28u));}
    U signed_less(U a,U b){return boolean(177,as_signed(a),as_signed(b));}
    U add(U a,U b){return binary(128,a,b);}U sub(U a,U b){return binary(130,a,b);}U mul(U a,U b){return binary(132,a,b);}
    U at(U base,U offset){return add(base,offset);}
    U load(U buffer,U offset){const auto pointer=value(65,storage_uint_,{buffers_[buffer],constant(0),offset});return value(61,uint_,{pointer});}
    void store(U buffer,U offset,U data){const auto pointer=value(65,storage_uint_,{buffers_[buffer],constant(0),offset});emit(code_,62,{pointer,data});}
    U parameter(U index){const auto pointer=value(65,push_uint_,{params_,constant(0),constant(index)});return value(61,uint_,{pointer});}
    void fault(U condition,Error error,U line,U column,U instruction){const auto first=both(equal(error_,constant(0)),condition);
        error_=choose(first,constant(static_cast<U>(error)),error_);line_=choose(first,constant(line),line_);column_=choose(first,constant(column),column_);instruction_=choose(first,constant(instruction),instruction_);}
    void fault(U condition,Error error,const smo::NativeInstruction& node,U index){fault(condition,error,node.line,node.column,index);}
    struct Product {U value,bad;};
    Product product(U a,U b){const auto sign_a=unequal(binary(199,a,constant(0x80000000u)),constant(0));const auto sign_b=unequal(binary(199,b,constant(0x80000000u)),constant(0));
        const auto negative=boolean(165,sign_a,sign_b);const auto absolute_a=choose(sign_a,sub(constant(0),a),a),absolute_b=choose(sign_b,sub(constant(0),b),b);
        const auto wide=value(151,mul_type_,{absolute_a,absolute_b}),low=value(81,uint_,{wide,0}),high=value(81,uint_,{wide,1});
        const auto limit=choose(negative,constant(0x80000000u),constant(0x7fffffffu));const auto bad=either(unequal(high,constant(0)),boolean(172,low,limit));
        const auto result=choose(negative,sub(constant(0),low),low);return {choose(bad,constant(0),result),bad};}
    U checked_add(U a,U b,bool subtraction,const smo::NativeInstruction& node,U index){const auto result=subtraction?sub(a,b):add(a,b),ab=binary(198,a,b),ar=binary(198,a,result);
        const auto mask=subtraction?ab:value(200,uint_,{ab});const auto overflow=unequal(binary(199,binary(199,mask,ar),constant(0x80000000u)),constant(0));
        fault(overflow,Error::overflow,node,index);return choose(overflow,constant(0),result);}
    U divide(U a,U b,const smo::NativeInstruction& node,U index){const auto zero=equal(b,constant(0)),overflow=both(equal(a,constant(0x80000000u)),equal(b,constant(0xffffffffu))),unsafe=either(zero,overflow);
        fault(zero,Error::division_zero,node,index);fault(overflow,Error::overflow,node,index);const auto safe_a=choose(unsafe,constant(0),a),safe_b=choose(unsafe,constant(1),b);
        const auto quotient=value(135,int_,{as_signed(safe_a),as_signed(safe_b)});
        // Exact divisibility is sign-independent. Match the HLSL/GLSL path
        // with unsigned magnitudes, including abs(INT_MIN)=0x80000000u.
        // safe_b is nonzero BEFORE UMod; SDiv keeps the signed quotient.
        const auto magnitude_a=choose(signed_less(safe_a,constant(0)),sub(constant(0),safe_a),safe_a);
        const auto magnitude_b=choose(signed_less(safe_b,constant(0)),sub(constant(0),safe_b),safe_b);
        const auto remainder=binary(137,magnitude_a,magnitude_b),q=value(124,uint_,{quotient}),fraction=unequal(remainder,constant(0));
        fault(fraction,Error::fraction,node,index);return choose(either(unsafe,fraction),constant(0),q);}
    U power(U a,U b,const smo::NativeInstruction& node,U index){const auto invalid=boolean(172,b,constant(64));fault(invalid,Error::exponent,node,index);const auto exponent=choose(invalid,constant(0),b);auto result=constant(1),factor=a;
        // Seven direct compiler-unrolled binary exponent steps cover0..64.
        // Unneeded products cannot report faults; no GPU instruction decoder.
        for(U bit=0;bit<7;++bit){const auto remaining=binary(194,exponent,constant(bit)),use=unequal(binary(199,remaining,constant(1)),constant(0));
            const auto next=product(result,factor);fault(both(use,next.bad),Error::overflow,node,index);result=choose(use,next.value,result);
            if(bit<6){const auto needed=boolean(172,remaining,constant(1));const auto square=product(factor,factor);fault(both(needed,square.bad),Error::overflow,node,index);factor=choose(needed,square.value,factor);}}
        return result;}
    void loop(U count,const std::function<void(U)>& body){if(!count)return;const auto preheader=block_,header=id(),inside=id(),continuation=id(),merge=id(),index=id(),next=id();emit(code_,249,{header});emit(code_,248,{header});block_=header;
        emit(code_,245,{uint_,index,constant(0),preheader,next,continuation});const auto condition=boolean(176,index,constant(count));emit(code_,246,{merge,continuation,0});emit(code_,250,{condition,inside,merge});
        emit(code_,248,{inside});block_=inside;body(index);emit(code_,249,{continuation});emit(code_,248,{continuation});block_=continuation;emit(code_,128,{uint_,next,index,constant(1)});emit(code_,249,{header});emit(code_,248,{merge});block_=merge;}
    void prepare(){const std::array<U,4> stride{static_cast<U>(program_.ir.states.size()),program_.ir.vertices*3u,program_.ir.faces*3u,smo::native_frame_words};
        for(U buffer=0;buffer<4;++buffer){destination_[buffer]=mul(add(mul(work_,count_),item_),constant(stride[buffer]));previous_[buffer]=mul(add(mul(old_,count_),item_),constant(stride[buffer]));}
        for(U buffer=0;buffer<3;++buffer)loop(stride[buffer],[&,buffer](U word){const auto old_value=action_==Action::reset?constant(0):load(buffer,at(previous_[buffer],word));store(buffer,at(destination_[buffer],word),old_value);});
        for(U word=0;word<smo::native_frame_words;++word){const U initial=(word>=3&&word<=5)?(real()?0x3f800000u:1u):word==6?program_.ir.vertices:word==7?program_.ir.faces:0u;frame_values_[word]=constant(initial);}
        error_=line_=column_=instruction_=constant(0);dt_=constant(0);if(action_==Action::step){
            if(real()){const auto raw=choose(equal(parameter(3),constant(1)),elapsed(),load(5,item_));smo::NativeInstruction time;time.line=time.column=0;classify(raw,time,0);
                const auto invalid=float_compare(184,raw,constant(0));fault(invalid,Error::time,0,0,0);dt_=choose(invalid,constant(0),raw);
            }else{const auto raw=load(5,item_),invalid=signed_less(raw,constant(0));fault(invalid,Error::time,0,0,0);dt_=choose(invalid,constant(0),raw);}}}
    void graph(){std::vector<U> values(graph_.value_count);U index=0;
        for(const auto& node:graph_.instructions){std::array<U,3> arguments{};for(unsigned a=0;a<arity(node.opcode);++a)arguments[a]=values[node.arguments[a]];U result=constant(0);
            switch(node.opcode){
            case Op::constant:result=constant(node.immediate);break;
            case Op::load_state:result=load(0,at(previous_[0],constant(node.immediate)));break;
            case Op::load_dt:result=dt_;break;
            case Op::positive:result=arguments[0];break;
            case Op::negate:{if(real())result=binary(198,arguments[0],constant(0x80000000u));else{const auto bad=equal(arguments[0],constant(0x80000000u));fault(bad,Error::overflow,node,index);result=choose(bad,constant(0),sub(constant(0),arguments[0]));}break;}
            case Op::add:result=real()?real_result(real_calculate(129,arguments[0],arguments[1]),node,index):checked_add(arguments[0],arguments[1],false,node,index);break;
            case Op::subtract:result=real()?real_result(real_calculate(131,arguments[0],arguments[1]),node,index):checked_add(arguments[0],arguments[1],true,node,index);break;
            case Op::multiply:{if(real())result=real_result(real_calculate(133,arguments[0],arguments[1]),node,index);else{const auto multiplied=product(arguments[0],arguments[1]);fault(multiplied.bad,Error::overflow,node,index);result=multiplied.value;}break;}
            case Op::divide:result=real()?real_divide(arguments[0],arguments[1],node,index):divide(arguments[0],arguments[1],node,index);break;
            case Op::power:result=real()?real_power(arguments[0],arguments[1],node,index):power(arguments[0],arguments[1],node,index);break;
            case Op::store_state:store(0,at(destination_[0],constant(node.immediate)),arguments[0]);break;
            case Op::vertex:case Op::face:{const auto buffer=node.opcode==Op::vertex?1u:2u;for(U axis=0;axis<3;++axis){auto stored=arguments[axis];
                    if(node.opcode==Op::face){if(real()){const auto limit=value(124,uint_,{value(112,float_,{constant(program_.ir.vertices)})});const auto range=both(float_compare(190,stored,constant(0)),float_compare(184,stored,limit));
                            const auto safe=choose(range,stored,constant(0)),converted=value(109,uint_,{as_float(safe)}),back=value(112,float_,{converted});const auto integral=boolean(180,as_float(stored),back);
                            fault(logical_not(both(range,integral)),Error::index,node,index);stored=converted;
                        }else fault(boolean(174,stored,constant(program_.ir.vertices)),Error::index,node,index);}
                    store(buffer,at(destination_[buffer],constant(node.immediate*3u+axis)),stored);}break;}
            case Op::rotation:case Op::color:for(U axis=0;axis<3;++axis){frame_values_[(node.opcode==Op::rotation?0u:3u)+axis]=arguments[axis];
                    if(node.opcode==Op::color){if(real())fault(logical_not(both(float_compare(190,arguments[axis],constant(0)),float_compare(188,arguments[axis],constant(0x3f800000u)))),Error::color,node,index);
                        else fault(boolean(172,arguments[axis],constant(1)),Error::color,node,index);}}break;
            case Op::draw:frame_values_[8]=constant(1);if(program_.ir.vertices<3||program_.ir.faces==0)fault(equal(constant(0),constant(0)),Error::geometry,node,index);break;
            }
            if(!terminal(node.opcode)){if(real()&&(node.opcode==Op::constant||node.opcode==Op::load_state||node.opcode==Op::load_dt))classify(result,node,index);values[node.result]=result;}++index;
        }
        // Header is compiler-local SSA until validation succeeds. Each final
        // frame word has exactly one UAV store: no redundant/default writes
        // can compete with authored rotation/color/draw after optimization.
        const auto commit=id(),done=id(),success=equal(error_,constant(0));emit(code_,247,{done,0});emit(code_,250,{success,commit,done});emit(code_,248,{commit});block_=commit;
        for(U word=0;word<smo::native_frame_words;++word)store(3,at(destination_[3],constant(word)),frame_values_[word]);emit(code_,249,{done});emit(code_,248,{done});block_=done;
        const auto base=mul(item_,constant(smo::native_status_words));store(4,base,error_);store(4,at(base,constant(1)),line_);store(4,at(base,constant(2)),column_);store(4,at(base,constant(3)),instruction_);
    }
public:
    Spirv(const smo::NativeProgram& program,Action action):program_(program),graph_(program.actions[action_index(action)]),action_(action){
        void_=id();bool_=id();uint_=id();int_=id();uint3_=id();function_=id();mul_type_=id();
        emit(types_,19,{void_});emit(types_,20,{bool_});emit(types_,21,{uint_,32,0});emit(types_,21,{int_,32,1});emit(types_,23,{uint3_,uint_,3});emit(types_,33,{function_,void_});emit(types_,30,{mul_type_,uint_,uint_});
        if(real()){float_=id();extension_=id();emit(types_,22,{float_,32});}
        const auto array=id(),structure=id(),pointer=id();storage_uint_=id();emit(types_,29,{array,uint_});emit(types_,30,{structure,array});emit(types_,32,{pointer,12,structure});emit(types_,32,{storage_uint_,12,uint_});
        emit(annotations_,71,{array,6,4});emit(annotations_,71,{structure,2});emit(annotations_,72,{structure,0,35,0});
        for(U binding=0;binding<8;++binding){buffers_[binding]=id();emit(types_,59,{pointer,buffers_[binding],12});emit(annotations_,71,{buffers_[binding],34,0});emit(annotations_,71,{buffers_[binding],33,binding});}
        const auto parameter_array=id(),parameter_structure=id(),parameter_pointer=id();push_uint_=id();const auto length=constant(16);
        emit(types_,28,{parameter_array,uint_,length});emit(types_,30,{parameter_structure,parameter_array});emit(types_,32,{parameter_pointer,9,parameter_structure});emit(types_,32,{push_uint_,9,uint_});params_=id();emit(types_,59,{parameter_pointer,params_,9});
        emit(annotations_,71,{parameter_array,6,4});emit(annotations_,71,{parameter_structure,2});emit(annotations_,72,{parameter_structure,0,35,0});
        const auto input_pointer=id();emit(types_,32,{input_pointer,1,uint3_});gid_=id();emit(types_,59,{input_pointer,gid_,1});emit(annotations_,71,{gid_,11,28});main_=id();
    }
    Words build(){emit(code_,54,{void_,main_,0,function_});block_=id();emit(code_,248,{block_});const auto coordinates=value(61,uint3_,{gid_});item_=value(81,uint_,{coordinates,0});count_=parameter(0);work_=parameter(1);old_=parameter(2);
        const auto inside=id(),merge=id(),in_range=boolean(176,item_,count_);emit(code_,247,{merge,0});emit(code_,250,{in_range,inside,merge});emit(code_,248,{inside});block_=inside;prepare();graph();emit(code_,249,{merge});emit(code_,248,{merge});emit(code_,253,{});emit(code_,56,{});
        Words result{0x07230203u,0x00010300u,0,next_,0};emit(result,17,{1});if(real()){Words import{extension_};const auto name=text("GLSL.std.450");import.insert(import.end(),name.begin(),name.end());emit(result,11,import);}emit(result,14,{0,1});Words entry{5,main_};const auto main_text=text("main");entry.insert(entry.end(),main_text.begin(),main_text.end());entry.push_back(gid_);emit(result,15,entry);emit(result,16,{main_,17,64,1,1});
        result.insert(result.end(),annotations_.begin(),annotations_.end());result.insert(result.end(),types_.begin(),types_.end());result.insert(result.end(),code_.begin(),code_.end());return result;}
};

std::unique_ptr<SmoCompute> create(const SmoNativeGpuOptions& options,std::stop_token stop){switch(options.api){case SmoComputeApi::direct3d11:return create_smo_d3d11_compute(options.device,stop);case SmoComputeApi::direct3d12:return create_smo_d3d12_compute(options.device,stop);
    case SmoComputeApi::vulkan:return create_smo_vulkan_compute(options.device,stop);case SmoComputeApi::opengl:return create_smo_opengl_compute(options.device,stop);}throw std::invalid_argument("API del compilador SMO desconocida.");}
std::size_t buffer_words(std::uint64_t count){if(count>0x3fffffffu)throw std::invalid_argument("Buffer nativo no direccionable con índices de32bits.");return static_cast<std::size_t>(std::max<std::uint64_t>(1,count));}
template<class T>std::span<const std::byte> bytes(std::span<T> data){return std::as_bytes(data);}
std::vector<U> read_words(SmoCompute& gpu,SmoBuffer buffer){const auto raw=gpu.read(buffer);if(raw.empty()||raw.size()%4u)throw std::runtime_error("Lectura nativa GPU truncada.");std::vector<U> words(raw.size()/4u);std::memcpy(words.data(),raw.data(),raw.size());return words;}
} //namespace

SmoKernelSource smo_native_kernel(const smo::NativeProgram& program,Action action){validate(program);const auto index=action_index(action);SmoKernelSource result;result.name="smo_native_action"+std::to_string(index)+"_"+program.identity;
    result.hlsl=smo::native_hlsl(program,action);result.glsl=smo::native_glsl(program,action);result.spirv=Spirv(program,action).build();return result;}

struct SmoNativeGpuProgram::Impl {
    smo::NativeProgram compiled;std::unique_ptr<SmoCompute> gpu;std::array<SmoKernel,3> kernels{};std::array<SmoBuffer,8> buffers{};std::array<std::size_t,7> sizes{};
    U items=0,bank=0;std::uint64_t revision=0;std::stop_token stop;
    Impl(smo::NativeProgram program,const SmoNativeGpuOptions& options,std::stop_token token):compiled(std::move(program)),items(options.items),stop(token){validate(compiled);if(!items||items>1048576u)throw std::invalid_argument("El lote nativo admite1..1048576 instancias.");
        canceled(stop);gpu=create(options,stop);for(unsigned action=0;action<3;++action){canceled(stop);kernels[action]=gpu->kernel(smo_native_kernel(compiled,static_cast<Action>(action)));}
        const auto twice=static_cast<std::uint64_t>(items)*2u;sizes={buffer_words(twice*compiled.ir.states.size()),buffer_words(twice*compiled.ir.vertices*3u),buffer_words(twice*compiled.ir.faces*3u),buffer_words(twice*smo::native_frame_words),buffer_words(static_cast<std::uint64_t>(items)*smo::native_status_words),buffer_words(items),buffer_words(compiled.scalarProfile==smo::NativeScalarProfile::ordinary_binary32?twice:0)};
        for(unsigned i=0;i<sizes.size();++i){const std::vector<U> zero(sizes[i],0);buffers[i]=gpu->buffer(sizes[i]*4u,bytes(std::span(zero)));}
    }
    SmoNativeGpuExecution execute(Action action,bool timed,U time_mode=0){canceled(stop);SmoNativeGpuExecution result;result.action=action;const auto begin=std::chrono::steady_clock::now();const U work=1u-bank;
        std::array<U,16> constants{items,work,bank,time_mode};const SmoDispatch dispatch{kernels[action_index(action)],buffers,constants,{(items+63u)/64u,1,1}};result.gpu_ms=gpu->execute(std::span(&dispatch,1),timed);
        const auto read_begin=std::chrono::steady_clock::now();const auto status=read_words(*gpu,buffers[4]);if(status.size()!=sizes[4])throw std::runtime_error("Estado de transacción GPU incompleto.");result.diagnostics.resize(items);bool success=true;
        for(U item=0;item<items;++item){const auto offset=static_cast<std::size_t>(item)*smo::native_status_words;auto& diagnostic=result.diagnostics[item];diagnostic={static_cast<Error>(status[offset]),status[offset+1],status[offset+2],status[offset+3]};if(diagnostic.code!=Error::none)success=false;}
        result.status_readback_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-read_begin).count();if(success){bank=work;++revision;}result.committed=success;result.revision=revision;
        result.wall_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();return result;}
};
SmoNativeGpuProgram::SmoNativeGpuProgram(std::unique_ptr<Impl> impl):impl_(std::move(impl)){}
SmoNativeGpuProgram::~SmoNativeGpuProgram()=default;SmoNativeGpuProgram::SmoNativeGpuProgram(SmoNativeGpuProgram&&) noexcept=default;SmoNativeGpuProgram& SmoNativeGpuProgram::operator=(SmoNativeGpuProgram&&) noexcept=default;
SmoNativeGpuProgram SmoNativeGpuProgram::compile(std::string_view source,const SmoNativeGpuOptions& options,std::stop_token stop,smo::Notation notation){return from_program(smo::native_compile(source,notation,options.scalar_profile),options,stop);}
SmoNativeGpuProgram SmoNativeGpuProgram::from_program(smo::NativeProgram program,const SmoNativeGpuOptions& options,std::stop_token stop){SmoNativeGpuProgram result(std::make_unique<Impl>(std::move(program),options,stop));const auto initialized=result.reset(false);
    if(!initialized.committed){const auto bad=std::ranges::find_if(initialized.diagnostics,[](const auto& diagnostic){return diagnostic.code!=Error::none;});throw std::runtime_error("Inicialización nativa GPU: error "+std::to_string(static_cast<U>(bad->code))+" en "+std::to_string(bad->line)+":"+std::to_string(bad->column)+".");}return result;}
SmoNativeGpuExecution SmoNativeGpuProgram::reset(bool timed){if(!impl_)throw std::logic_error("Programa nativo movido.");return impl_->execute(Action::reset,timed);}
SmoNativeGpuExecution SmoNativeGpuProgram::frame(bool timed){if(!impl_)throw std::logic_error("Programa nativo movido.");return impl_->execute(Action::frame,timed);}
SmoNativeGpuExecution SmoNativeGpuProgram::step(std::span<const std::int32_t> dt,bool timed){if(!impl_)throw std::logic_error("Programa nativo movido.");if(dt.size()!=impl_->items)throw std::invalid_argument("Falta un dt entero por instancia.");
    if(impl_->compiled.scalarProfile!=smo::NativeScalarProfile::ordinary_i32)throw std::invalid_argument("step int32 requiere el perfil entero.");
    canceled(impl_->stop);const auto begin=std::chrono::steady_clock::now();impl_->gpu->write(impl_->buffers[5],bytes(dt));auto result=impl_->execute(Action::step,timed);
    result.wall_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();return result;}
void SmoNativeGpuProgram::seed_states(std::span<const std::int32_t> values){if(!impl_)throw std::logic_error("Programa nativo movido.");auto& r=*impl_;const auto count=static_cast<std::size_t>(r.items)*r.compiled.ir.states.size();if(values.size()!=count)throw std::invalid_argument("Falta un estado entero por instancia/slot.");
    if(r.compiled.scalarProfile!=smo::NativeScalarProfile::ordinary_i32)throw std::invalid_argument("La carga int32 requiere el perfil entero.");
    canceled(r.stop);auto raw=read_words(*r.gpu,r.buffers[0]);if(raw.size()!=r.sizes[0])throw std::runtime_error("Estados nativos GPU incompletos.");const auto offset=static_cast<std::size_t>(r.bank)*count;
    for(std::size_t i=0;i<count;++i)raw[offset+i]=std::bit_cast<U>(values[i]);r.gpu->write(r.buffers[0],bytes(std::span(raw)));++r.revision;}
SmoNativeGpuSnapshot SmoNativeGpuProgram::inspect()const{if(!impl_)throw std::logic_error("Programa nativo movido.");auto& r=*impl_;canceled(r.stop);std::array<std::vector<U>,4> raw;
    if(r.compiled.scalarProfile!=smo::NativeScalarProfile::ordinary_i32)throw std::invalid_argument("inspect int32 requiere el perfil entero.");
    for(unsigned buffer=0;buffer<4;++buffer){raw[buffer]=read_words(*r.gpu,r.buffers[buffer]);if(raw[buffer].size()!=r.sizes[buffer])throw std::runtime_error("Snapshot nativo GPU incompleto.");}
    SmoNativeGpuSnapshot result;result.revision=r.revision;result.items.resize(r.items);for(U item=0;item<r.items;++item){auto& frame=result.items[item];const auto linear=static_cast<std::size_t>(r.bank)*r.items+item;
        const auto states=linear*r.compiled.ir.states.size();for(std::size_t state=0;state<r.compiled.ir.states.size();++state)frame.states.push_back(std::bit_cast<std::int32_t>(raw[0][states+state]));const auto header=linear*smo::native_frame_words;
        if(raw[3][header+6]>r.compiled.ir.vertices||raw[3][header+7]>r.compiled.ir.faces||raw[3][header+8]>1||raw[3][header+9])throw std::runtime_error("Cabecera nativa GPU inválida.");
        for(U axis=0;axis<3;++axis){frame.rotation[axis]=std::bit_cast<std::int32_t>(raw[3][header+axis]);frame.color[axis]=std::bit_cast<std::int32_t>(raw[3][header+3+axis]);}frame.draw_requested=raw[3][header+8]!=0;
        for(U vertex=0;vertex<raw[3][header+6];++vertex){std::array<std::int32_t,3> v{};for(U axis=0;axis<3;++axis)v[axis]=std::bit_cast<std::int32_t>(raw[1][linear*r.compiled.ir.vertices*3u+vertex*3u+axis]);frame.vertices.push_back(v);}
        for(U face=0;face<raw[3][header+7];++face){std::array<U,3> indices{};for(U axis=0;axis<3;++axis)indices[axis]=raw[2][linear*r.compiled.ir.faces*3u+face*3u+axis];frame.faces.push_back(indices);}}
    return result;}
SmoNativeGpuExecution SmoNativeGpuProgram::step_binary32(std::span<const U> bits,bool timed){if(!impl_)throw std::logic_error("Programa nativo movido.");auto& r=*impl_;
    if(r.compiled.scalarProfile!=smo::NativeScalarProfile::ordinary_binary32||bits.size()!=r.items)throw std::invalid_argument("Se requiere un dt binary32 por instancia del perfil real.");
    canceled(r.stop);const auto begin=std::chrono::steady_clock::now();r.gpu->write(r.buffers[5],bytes(bits));auto result=r.execute(Action::step,timed);result.wall_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();return result;}
SmoNativeGpuExecution SmoNativeGpuProgram::step_elapsed(std::span<const std::uint64_t> nanoseconds,bool timed){if(!impl_)throw std::logic_error("Programa nativo movido.");auto& r=*impl_;
    if(r.compiled.scalarProfile!=smo::NativeScalarProfile::ordinary_binary32||nanoseconds.size()!=r.items)throw std::invalid_argument("Se requiere un reloj uint64 por instancia del perfil real.");
    canceled(r.stop);const auto begin=std::chrono::steady_clock::now();std::vector<U> words;words.reserve(nanoseconds.size()*2u);for(const auto ns:nanoseconds){words.push_back(static_cast<U>(ns));words.push_back(static_cast<U>(ns>>32u));}
    r.gpu->write(r.buffers[6],bytes(std::span(words)));auto result=r.execute(Action::step,timed,1);result.wall_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();return result;}
void SmoNativeGpuProgram::seed_binary32_states(std::span<const U> bits){if(!impl_)throw std::logic_error("Programa nativo movido.");auto& r=*impl_;const auto count=static_cast<std::size_t>(r.items)*r.compiled.ir.states.size();
    if(r.compiled.scalarProfile!=smo::NativeScalarProfile::ordinary_binary32||bits.size()!=count)throw std::invalid_argument("Falta un estado binary32 por instancia/slot.");
    canceled(r.stop);auto raw=read_words(*r.gpu,r.buffers[0]);if(raw.size()!=r.sizes[0])throw std::runtime_error("Estados binary32 GPU incompletos.");const auto offset=static_cast<std::size_t>(r.bank)*count;
    std::copy(bits.begin(),bits.end(),raw.begin()+static_cast<std::ptrdiff_t>(offset));r.gpu->write(r.buffers[0],bytes(std::span(raw)));++r.revision;}
SmoNativeGpuRealSnapshot SmoNativeGpuProgram::inspect_binary32()const{if(!impl_)throw std::logic_error("Programa nativo movido.");auto& r=*impl_;if(r.compiled.scalarProfile!=smo::NativeScalarProfile::ordinary_binary32)throw std::invalid_argument("inspect_binary32 requiere el perfil real.");
    canceled(r.stop);std::array<std::vector<U>,4> raw;for(unsigned buffer=0;buffer<4;++buffer){raw[buffer]=read_words(*r.gpu,r.buffers[buffer]);if(raw[buffer].size()!=r.sizes[buffer])throw std::runtime_error("Snapshot binary32 GPU incompleto.");}
    SmoNativeGpuRealSnapshot result;result.revision=r.revision;result.items.resize(r.items);for(U item=0;item<r.items;++item){auto& frame=result.items[item];const auto linear=static_cast<std::size_t>(r.bank)*r.items+item;
        const auto states=linear*r.compiled.ir.states.size();for(std::size_t state=0;state<r.compiled.ir.states.size();++state)frame.states.push_back(raw[0][states+state]);const auto header=linear*smo::native_frame_words;
        if(raw[3][header+6]>r.compiled.ir.vertices||raw[3][header+7]>r.compiled.ir.faces||raw[3][header+8]>1||raw[3][header+9])throw std::runtime_error("Cabecera binary32 GPU inválida.");
        for(U axis=0;axis<3;++axis){frame.rotation[axis]=raw[3][header+axis];frame.color[axis]=raw[3][header+3+axis];}frame.draw_requested=raw[3][header+8]!=0;
        for(U vertex=0;vertex<raw[3][header+6];++vertex){std::array<U,3> v{};for(U axis=0;axis<3;++axis)v[axis]=raw[1][linear*r.compiled.ir.vertices*3u+vertex*3u+axis];frame.vertices.push_back(v);}
        for(U face=0;face<raw[3][header+7];++face){std::array<U,3> indices{};for(U axis=0;axis<3;++axis)indices[axis]=raw[2][linear*r.compiled.ir.faces*3u+face*3u+axis];frame.faces.push_back(indices);}}
    return result;}
SmoNativeGpuFrameView SmoNativeGpuProgram::frame_view(U item)const{if(!impl_)throw std::logic_error("Programa nativo movido.");const auto& r=*impl_;if(item>=r.items)throw std::out_of_range("Instancia gráfica fuera del lote.");
    const auto linear=static_cast<std::uint64_t>(r.bank)*r.items+item;return {r.buffers[1],r.buffers[2],r.buffers[3],static_cast<U>(linear*r.compiled.ir.vertices*3u),static_cast<U>(linear*r.compiled.ir.faces*3u),static_cast<U>(linear*smo::native_frame_words),r.compiled.ir.vertices,r.compiled.ir.faces,r.compiled.scalarProfile,r.revision};}
SmoCompute& SmoNativeGpuProgram::compute(){if(!impl_)throw std::logic_error("Programa nativo movido.");return *impl_->gpu;}
const smo::NativeProgram& SmoNativeGpuProgram::program()const{if(!impl_)throw std::logic_error("Programa nativo movido.");return impl_->compiled;}
SmoComputeInfo SmoNativeGpuProgram::info()const{if(!impl_)throw std::logic_error("Programa nativo movido.");return impl_->gpu->info();}
std::uint32_t SmoNativeGpuProgram::item_count()const{if(!impl_)throw std::logic_error("Programa nativo movido.");return impl_->items;}
std::uint64_t SmoNativeGpuProgram::revision()const{if(!impl_)throw std::logic_error("Programa nativo movido.");return impl_->revision;}
} //namespace lgaed::presentation
