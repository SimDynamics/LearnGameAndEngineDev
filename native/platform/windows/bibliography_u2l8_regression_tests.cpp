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
