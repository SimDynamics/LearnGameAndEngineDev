#include "core/image_resource.hpp"
#include "core/text_semantics.hpp"
#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace lgaed::curriculum {
namespace {
void require(bool valid,const char* message){if(!valid)throw std::invalid_argument(message);}
std::string lower(std::string_view value){std::string result(value);for(auto& c:result)if(c>='A'&&c<='Z')c+=32;return result;}
bool blank(std::string_view text){
    validate_text_semantics(TextSemantics{std::string(text),{}});
    for(std::size_t at=0;at<text.size();){
        const auto lead=static_cast<unsigned char>(text[at++]);unsigned cp=lead;
        const auto extra=lead<0x80?0:lead<0xe0?1:lead<0xf0?2:3;
        if(extra){cp=lead&((1u<<(6-extra))-1);for(int n=0;n<extra;++n)cp=(cp<<6)|(static_cast<unsigned char>(text[at++])&63);}
        if(!((cp>=9&&cp<=13)||(cp>=28&&cp<=32)||cp==0x85||cp==0xa0||cp==0x1680||
            (cp>=0x2000&&cp<=0x200a)||cp==0x2028||cp==0x2029||cp==0x202f||cp==0x205f||cp==0x3000))return false;
    }return true;
}
}
void validate_image_resource(const ImageResource& value){
    require(!value.image_id.empty()&&value.image_id.size()<=128&&std::ranges::all_of(value.image_id,[](unsigned char c){return
        (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_';}),"Invalid curricular imageId");
    require(!blank(value.alt),"A curricular image requires nonblank alt text");
    require(value.caption.empty()||!blank(value.caption),"A curricular image caption must not be blank");
    require(value.alt.find('\0')==value.alt.npos&&value.caption.find('\0')==value.caption.npos,"An image description cannot contain NUL");
    require(value.src.starts_with("images/")&&value.src.size()<=1024,"Image src must be relative to content/images/");
    require(std::ranges::none_of(value.src,[](unsigned char c){return c<32||c==127||std::string_view("\\:?#*\"<>|").find(c)!=std::string_view::npos;}),"Invalid curricular image path character");
    for(std::size_t at=0;at<value.src.size();){
        const auto slash=value.src.find('/',at),last=slash==value.src.npos?value.src.size():slash;
        const auto part=std::string_view(value.src).substr(at,last-at);
        require(!part.empty()&&part!="."&&part!=".."&&part.back()!='.'&&part.back()!=' ',"Invalid curricular image path segment");
        const auto name=lower(part.substr(0,part.find('.')));
        require(name!="con"&&name!="prn"&&name!="aux"&&name!="nul"&&name!="clock$"&&name!="conin$"&&name!="conout$"&&
            name!="com¹"&&name!="com²"&&name!="com³"&&name!="lpt¹"&&name!="lpt²"&&name!="lpt³"&&
            !(name.size()==4&&(name.starts_with("com")||name.starts_with("lpt"))&&name[3]>='1'&&name[3]<='9'),"Reserved curricular image path segment");
        if(slash==value.src.npos)break;at=slash+1;require(at<value.src.size(),"Empty curricular image filename");
    }
    const auto filename=std::string_view(value.src).substr(value.src.find_last_of('/')+1);
    const auto dot=filename.find_last_of('.');
    require(dot!=filename.npos&&filename.substr(0,dot)==value.image_id,"Image filename must match imageId");
    const auto extension=lower(filename.substr(dot));
    require(extension==".png"||extension==".jpg"||extension==".jpeg","Only PNG and JPEG curricular images are supported");
}
}
