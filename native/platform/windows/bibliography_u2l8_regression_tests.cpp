// Published U2L8 bibliography assertions run against the original Windows
// matcher. This fixture reads the untouched JSON, not a recreated citation.
#include "platform/windows/bibliography_revision.hpp"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <filesystem>
#include <array>
#include <utility>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::invalid_argument("Supply the actual U2L8 JSON path");
        const auto file=std::filesystem::path(argv[1]);
        std::ifstream input(file,std::ios::binary);
        if(!input)throw std::runtime_error("Cannot read published U2L8 lesson");
        const std::string bytes{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
        winrt::init_apartment();
        using namespace winrt::Windows::Data::Json;
        // The production parser must distinguish numeric ranges from altered
        // meanings; ASCII, en dash and em dash spell the same locator interval.
        for(const auto tail:std::array<std::string_view,7>{{
                "sección 5.13.3","párr. 1–3","párrs. 1–3",
                "párrs. 1—3","párrs. 1-3",
                "sección 5.13.3, párrs. 1–3 y tabla 9",
                "sección 5.13.3, párr. 4 y tabla 10"}}){
            const auto before=std::string("Una explicación (Autor, 2024, ")+
                std::string(tail)+").";
            const auto after=std::string("Una explicación (Autor, 2024).");
            const auto forward=lgaed::windows::bibliography::only_parenthetical_locator_removal(before,after);
            const auto reverse=lgaed::windows::bibliography::only_parenthetical_locator_removal(after,before);
            if(!forward||reverse) {
                auto ascii=before;
                auto replace_range=[&](std::string_view needle){
                    for(std::size_t pos=0;(pos=ascii.find(needle,pos))!=std::string::npos;){
                        ascii.replace(pos,needle.size(),"-");
                        ++pos;
                    }
                };
                replace_range("–");replace_range("—");
                const auto ascii_forward=lgaed::windows::bibliography::only_parenthetical_locator_removal(ascii,after);
                std::cerr<<"Bibliography range debug: singular/original="<<before
                    <<" original="<<forward<<" reverse="<<reverse
                    <<" ascii="<<ascii_forward<<" asciiBefore="<<ascii<<"\n";
                throw std::runtime_error("Valid bibliography range failed or allowed reversal: "+std::string(tail));
            }
        }
        for(const auto tail:std::array<std::string_view,3>{{
                "párrs. 1–tres","párrs. uno–3","párrs. 1–3 y texto libre"}}){
            const auto before=std::string("Una explicación (Autor, 2024, ")+
                std::string(tail)+").";
            if(lgaed::windows::bibliography::only_parenthetical_locator_removal(
                before,"Una explicación (Autor, 2024)."))
                throw std::runtime_error("Invalid locator text was silently admitted: "+std::string(tail));
        }
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
            if(!approved)
                throw std::runtime_error("Published locator-only revision rejected: owner="+
                    owner+" ordinal="+std::to_string(index));
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
