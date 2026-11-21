// Published U2L8 bibliography assertions run against the original Windows
// matcher. This fixture reads the untouched JSON, not a recreated citation.
#include "platform/windows/bibliography_revision.hpp"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::invalid_argument("Supply the actual U2L8 JSON path");
        const auto file=std::filesystem::path(argv[1]);
        std::ifstream input(file,std::ios::binary);
        if(!input)throw std::runtime_error("Cannot read published U2L8 lesson");
        const std::string bytes{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
        winrt::init_apartment();
        using namespace winrt::Windows::Data::Json;
        const auto root=JsonObject::Parse(winrt::to_hstring(bytes));
        const auto edges=root.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial");
        std::size_t checked=0;
        for(std::uint32_t index=0;index<edges.Size();++index) {
            const auto item=edges.GetObjectAt(index);
            if(!item.HasKey(L"bibliography"))continue;
            const auto owner=winrt::to_string(item.GetNamedString(L"id"));
            const auto archive=item.GetNamedObject(L"bibliography");
            const auto before=winrt::to_string(
                archive.GetNamedObject(L"previousBlock").GetNamedString(L"explanation"));
            const auto after=winrt::to_string(
                archive.GetNamedObject(L"currentBlock").GetNamedString(L"explanation"));
            const bool approved=lgaed::windows::bibliography::only_parenthetical_locator_removal(before,after);
            if(!approved) {
                const auto probe=[&](std::string_view name,const std::string& old_text,
                                     const std::string& new_text) {
                    std::cerr<<"Bibliography subcase "<<name<<" valid="
                        <<lgaed::windows::bibliography::only_parenthetical_locator_removal(old_text,new_text)
                        <<"\n";
                };
                const auto old_citation=before.rfind("(Köppe");
                const auto new_citation=after.rfind("(Köppe");
                if(old_citation!=std::string::npos&&new_citation!=std::string::npos) {
                    probe("just-citation","Una explicación "+before.substr(old_citation),
                          "Una explicación "+after.substr(new_citation));
                    const auto old_paragraph=before.rfind("\n\n");
                    const auto new_paragraph=after.rfind("\n\n");
                    if(old_paragraph!=std::string::npos&&new_paragraph!=std::string::npos)
                        probe("last-paragraph",before.substr(old_paragraph+2),
                              after.substr(new_paragraph+2));
                    auto old_first=before,new_first=after;
                    const auto old_earlier=old_first.find("(Stroustrup, 2024)");
                    const auto new_earlier=new_first.find("(Stroustrup, 2024)");
                    if(old_earlier!=std::string::npos&&new_earlier!=std::string::npos) {
                        old_first.replace(old_earlier,24,"[primera cita]");
                        new_first.replace(new_earlier,24,"[primera cita]");
                        probe("first-citation-masked",old_first,new_first);
                    }
                }
                std::cerr<<"Rejected bibliographic source: owner="<<owner
                    <<" ordinal="<<index<<" before="<<before<<" after="<<after<<"\n";
                throw std::runtime_error("Published optional-locator removal was rejected");
            }
            if(lgaed::windows::bibliography::only_parenthetical_locator_removal(after,before)||
               lgaed::windows::bibliography::only_parenthetical_locator_removal(before,before))
                throw std::runtime_error("Bibliographic removal admitted an expansion or unchanged text");
            ++checked;
        }
        if(checked!=8)throw std::runtime_error("U2L8 published bibliography inventory changed");
        std::cout<<"PASS U2L8: "<<checked
            <<" independently authorized bibliography-only changes, using actual Windows parser\n";
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<"FAIL U2L8 published bibliography: "<<error.what()<<"\n";
        return 1;
    }
}
