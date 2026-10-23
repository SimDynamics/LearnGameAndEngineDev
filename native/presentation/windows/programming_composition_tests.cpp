#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/native_math_text.hpp"
#include "platform/windows/catalog_loader.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/narration_worker.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"
#include <commctrl.h>
#include <richedit.h>
#include <winrt/base.h>
#include <algorithm>
#include <array>
#include <exception>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
using curriculum::BlockType;
void require(bool value,const std::string& reason){if(!value)throw std::runtime_error(reason);}
int id(std::size_t block,int part){return 1000+static_cast<int>(block)*100+part;}
HWND child(HWND parent,int value){struct Search{int id;HWND found=nullptr;} current{value};
    EnumChildWindows(parent,[](HWND item,LPARAM data)->BOOL{auto& search=*reinterpret_cast<Search*>(data);if(GetDlgCtrlID(item)==search.id){search.found=item;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&current));return current.found;}
std::string caption(HWND item){const auto length=GetWindowTextLengthW(item);std::wstring value(static_cast<std::size_t>(length)+1,L'\0');value.resize(static_cast<std::size_t>(GetWindowTextW(item,value.data(),static_cast<int>(value.size()))));return utf8(value);}
std::string name(BlockType type){switch(type){case BlockType::heading:return "heading";case BlockType::paragraph:return "paragraph";case BlockType::note:return "note";case BlockType::code:return "code";case BlockType::multiple_choice:return "multiple_choice";case BlockType::output_prediction:return "output_prediction";case BlockType::fill_code:return "fill_code";case BlockType::order_code:return "order_code";case BlockType::cpp_compiler:return "cpp_compiler";case BlockType::inline_fill_code:return "inline_fill_code";case BlockType::drag_fill_code:return "drag_fill_code";case BlockType::math_puzzle:return "math_puzzle";}throw std::runtime_error("Unknown authored block type");}
std::string plain(std::string_view text){std::string result;for(std::size_t i=0;i<text.size();){
    if(text[i]=='`'){const auto end=text.find('`',i+1);if(end!=text.npos&&end>i+1&&text.substr(i+1,end-i-1).find_first_of("\r\n")==text.npos){result+=text.substr(i+1,end-i-1);i=end+1;continue;}}
    if(text[i]=='\r'){result+='\n';i+=i+1<text.size()&&text[i+1]=='\n'?2:1;}else result+=text[i++];}return result;}
std::string line_endings(std::string_view text){std::string result;for(std::size_t i=0;i<text.size();++i){if(text[i]=='\r'){result+='\n';if(i+1<text.size()&&text[i+1]=='\n')++i;}else result+=text[i];}return result;}
struct Window{HWND value=nullptr;~Window(){if(IsWindow(value))DestroyWindow(value);}};
struct OwnedQa{
    std::filesystem::path root,parent;
    explicit OwnedQa(const std::filesystem::path& value):root(value),parent(std::filesystem::weakly_canonical(value.parent_path())){
        require(root.is_absolute()&&root.parent_path().filename()==L"qa"&&root.filename().wstring().starts_with(L"composition-"+std::to_wstring(GetCurrentProcessId())+L"-")&&!std::filesystem::exists(root),"Composition QA must own a new isolated profile");}
    ~OwnedQa() noexcept(false){if(std::uncaught_exceptions()!=0)return;const auto resolved=std::filesystem::weakly_canonical(root);
        require(resolved.parent_path()==parent&&resolved.filename()==root.filename(),"Refuse cleanup outside this exact composition QA profile");state::require_safe_path(resolved);
        for(const auto& entry:std::filesystem::recursive_directory_iterator(state::native_io_path(resolved)))state::require_safe_path(entry.path());
        std::filesystem::remove_all(state::native_io_path(resolved));require(!std::filesystem::exists(root),"Successful composition QA retained generated data");}
};
struct PublishedLesson{std::string route,course;std::filesystem::path path;std::string locale;};
std::vector<PublishedLesson> published(const std::filesystem::path& root,const std::string& locale){
    std::vector<PublishedLesson> result;std::set<std::filesystem::path> seen;const curriculum::CatalogLocation home{locale};
    for(const auto& route:windows::load_catalog(root,home).entries)if(route.available){const auto route_location=curriculum::catalog_child(home,route.id);
        for(const auto& course:windows::load_catalog(root,route_location).entries)if(course.available){const auto course_location=curriculum::catalog_child(route_location,course.id);
            for(const auto& unit:windows::load_catalog(root,course_location).entries)if(unit.available){const auto unit_location=curriculum::catalog_child(course_location,unit.id);
                for(const auto& lesson:windows::load_catalog(root,unit_location).entries)if(lesson.available&&seen.insert(lesson.asset).second)result.push_back({route.id,course.id,lesson.asset,lesson.asset_locale});}}}
    require(!result.empty(),"The published curriculum census is empty");return result;
}
struct Counts{std::size_t lessons=0,pages=0,blocks=0,projects=0,cmake=0,stdin_fields=0,output_checks=0,math_containers=0,math_projections=0;std::map<BlockType,std::size_t> types;std::set<std::string> routes,asset_locales;};
void stdin_metadata_contract(){
    const auto parse=[](std::string_view fields){const auto json=std::string(R"json({"id":"qa_stdin_contract","title":"QA","pages":[{"blocks":[{"type":"cpp_compiler","initialCode":"int main(){}")json")+std::string(fields)+"}]}]}";return windows::parse_lesson_json(json).pages.front().blocks.front();};
    require(!parse("").standard_input,"An activity without input metadata invented a stdin field");
    const auto explicit_empty=parse(R"json(,"initialStandardInput":"")json");require(explicit_empty.standard_input&&explicit_empty.standard_input->empty(),"An explicitly empty prepared input field disappeared");
    const auto inferred=parse(R"json(,"outputCheck":{"cases":[{"standardInput":"Leo\n2\n","outputs":["7"]}]})json");
    require(inferred.standard_input&&inferred.standard_input->empty()&&inferred.output_check->cases.front().standard_input=="Leo\n2\n","An authored nonempty check case did not enable an empty input field, or disclosed its expected input as a seed");
    const auto no_input=parse(R"json(,"outputCheck":{"cases":[{"standardInput":"","outputs":["7"]}]})json");require(!no_input.standard_input,"Empty output-check input cases invented an unnecessary stdin field");
    const std::string deliberate=" \t \n    ";const auto whites=parse(R"json(,"initialStandardInput":" \t \n    ")json");
    require(whites.standard_input&&*whites.standard_input==deliberate,"The input metadata parser removed deliberate spaces, tabs or line breaks");
    state::StateData data;state::Draft draft{"int main(){}",deliberate};state::put_draft(data,"qa/declared-stdin",draft);
    const auto persisted=state::decode(state::encode(data));require(state::get_draft(persisted,"qa/declared-stdin","fallback","").input==deliberate,"Draft serialization changed intentionally whitespace-sensitive stdin");
    std::cout<<"PASS stdin metadata: explicit empty / inferred blank / absent / authored and user whitespace preserved\n";
}
void assert_page(HWND view,const curriculum::Lesson& model,const std::string& ui_locale,Counts& counts){
    HWND pane=nullptr;EnumChildWindows(view,[](HWND item,LPARAM target)->BOOL{wchar_t type[128]{};GetClassNameW(item,type,128);if(std::wstring_view(type)==L"LearnGameAndEngineDev.LessonPane"){*reinterpret_cast<HWND*>(target)=item;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&pane));require(pane!=nullptr,"The composed page has no lesson viewport");
    // IDs belong to their native parent. A project's file tabs or a puzzle's
    // own fields may legitimately reuse another parent's integer IDs.
    const auto control=[&](std::size_t index,int part){const auto wanted=id(index,part);if(const auto direct=GetDlgItem(pane,wanted))return direct;
        struct Search{int id;HWND item=nullptr;} found{wanted};
        EnumChildWindows(pane,[](HWND candidate,LPARAM raw)->BOOL{auto& value=*reinterpret_cast<Search*>(raw);wchar_t owner[128]{};
            GetClassNameW(GetParent(candidate),owner,128);
            if(GetDlgCtrlID(candidate)==value.id&&std::wstring_view(owner)==L"LearnGameAndEngineDev.LessonPane"){value.item=candidate;return FALSE;}return TRUE;
        },reinterpret_cast<LPARAM>(&found));return found.item;};
    const auto& page=model.pages.front();const auto regions=lesson_reading_regions(view);const auto required=[&](std::size_t index,int part,const std::string& role){const auto item=control(index,part);require(item!=nullptr,"Missing "+role+" for "+name(page.blocks.at(index).type)+" at block"+std::to_string(index));RECT bounds{};GetWindowRect(item,&bounds);require(bounds.right>bounds.left&&bounds.bottom>bounds.top,"Zero-size "+role+" at block"+std::to_string(index));return item;};
    const auto narrates=[&](HWND target,std::string_view source){return std::ranges::any_of(regions,[&](const auto& region){return region.target==target&&region.source_text==source&&!region.feedback;});};
    const auto exact=[&](HWND target,std::string_view source,bool code){
        if(!code)if(const auto mapped=native_math_text_mapping(target,source)){
            // TOM legitimately replaces LaTeX with its native mathematical
            // structure. Verify the original owner and its actual current
            // UTF16 projection; never accept arbitrary altered plain text.
            const auto original=wide(source);const auto& offsets=mapped->source_to_native;
            require(mapped->source==source&&offsets.size()==original.size()+1&&!offsets.empty()&&offsets.front()==0&&offsets.back()==mapped->native_text.size(),"The native mathematical display lost its complete original source mapping");
            for(std::size_t at=0;at<offsets.size();++at){const auto offset=offsets[at];
                require(offset<=mapped->native_text.size()&&(at==0||offset>=offsets[at-1]),"A native mathematical source range is out of bounds or nonmonotonic");
                require(!(offset>0&&offset<mapped->native_text.size()&&IS_HIGH_SURROGATE(mapped->native_text[offset-1])&&IS_LOW_SURROGATE(mapped->native_text[offset])),"A native mathematical source range splits a displayed Unicode character");}
            ++counts.math_projections;return;
        }
        const auto actual=edit_text(target),expected=code?line_endings(source):plain(source);
        const auto diagnostic=[](std::string_view text){std::string value;for(const auto c:text.substr(0,320)){if(c=='\n')value+="\\n";else if(c=='\r')value+="\\r";else if(c=='\t')value+="\\t";else value+=c;}if(text.size()>320)value+="...";return value;};
        require(actual==expected,"Displayed characters differ from their original authored "+std::string(code?"code":"text")+"; expected="+diagnostic(expected)+"; actual="+diagnostic(actual));
    };
    const auto no_reading=[&](HWND target){require(std::ranges::none_of(regions,[&](const auto& region){return region.target==target;}),"Source/console or unanswered feedback entered the narration script");};
    const auto check_caption=[&](std::size_t index,int part=16){const auto header=required(index,part,"console caption");require(edit_text(header).starts_with("</> "),"The authored source/puzzle lost its console caption");if(!reader_theme::palette().high_contrast)require(reader_theme::background(header)==RGB(35,35,35),"The console caption lost its Android header surface");};
    for(std::size_t index=0;index<page.blocks.size();++index){const auto& block=page.blocks[index];++counts.blocks;++counts.types[block.type];
        if(block.type==BlockType::heading||block.type==BlockType::paragraph||block.type==BlockType::note){const auto text=required(index,0,"authored text");exact(text,block.text,false);if(!block.text.empty())require(narrates(text,block.text),"A visible title, paragraph or note was omitted from narration");
            if(block.type==BlockType::note){const auto heading=required(index,19,"note heading");const auto expected=ui_locale=="es"?"Nota":"Note";exact(heading,expected,false);require(narrates(heading,expected),"The note heading was omitted from narration");}continue;}
        if(block.type==BlockType::code){const auto source=required(index,1,"read-only source");exact(source,block.code,true);require((GetWindowLongPtrW(source,GWL_STYLE)&ES_READONLY)!=0,"A read-only example became editable");no_reading(source);check_caption(index);continue;}
        if(!block.question.empty()&&block.type!=BlockType::math_puzzle){const auto question=required(index,0,"question");exact(question,block.question,false);require(narrates(question,block.question),"An authored question was omitted from narration");}
        if(block.type==BlockType::cpp_compiler){const auto editor=required(index,10,"native C++ editor"),run=required(index,11,"compiler action"),console=required(index,14,"program console");check_caption(index);
            require((GetWindowLongPtrW(editor,GWL_STYLE)&ES_READONLY)==0,"A fresh C++ editor is not editable");std::string original=block.initial_code;
            if(!block.project_files.empty()){++counts.projects;required(index,5,"multi-file project toolbar");const auto selected=std::ranges::find(block.project_files,std::string("main.cpp"),[](const auto& file)->const std::string&{return file.first;});original=selected!=block.project_files.end()?selected->second:block.project_files.front().second;}
            exact(editor,original,true);no_reading(editor);no_reading(console);
            const auto fresh_console=edit_text(console);
            require(fresh_console.empty(),"A fresh composition reused a previous console result at block"+std::to_string(index)+
                "; cmake="+std::to_string(block.cmake.has_value())+"; actual="+fresh_console);
            require((control(index,15)!=nullptr)==block.standard_input.has_value(),"Prepared input visibility is not driven by authored metadata");
            if(block.standard_input){++counts.stdin_fields;exact(required(index,15,"prepared stdin"),*block.standard_input,true);const auto input_label=required(index,17,"input label");exact(input_label,ui_locale=="es"?"Entrada":ui_locale=="fr"?"Entrée":"Input",false);}
            if(block.cmake){++counts.cmake;const auto config=required(index,6,"CMake configuration");require(child(config,400)&&child(config,402)&&child(config,404)&&child(config,405)&&child(config,406),"A published CMake project omitted its target/options/configure/build controls");}
            const auto expected=block.output_check?(ui_locale=="es"?"Compilar, ejecutar y comprobar":ui_locale=="fr"?"Compiler, exécuter et vérifier":"Compile, run and check"):
                block.cmake?(ui_locale=="es"?"Ejecutar objetivo construido":ui_locale=="fr"?"Exécuter la cible construite":"Run built target"):
                (ui_locale=="es"?"Compilar y ejecutar":ui_locale=="fr"?"Compiler et exécuter":"Compile and run");
            require(caption(run)==expected,"The compiler action does not match the authored execution/output-check contract");if(block.output_check)++counts.output_checks;
        }else{
            required(index,3,"Check action");
            if(block.type==BlockType::math_puzzle){++counts.math_containers;required(index,5,"mathematical puzzle container");}
            else{if(block.type==BlockType::fill_code||block.type==BlockType::output_prediction){check_caption(index,38);if(!block.code.empty())check_caption(index);}else check_caption(index);
                if(!block.code.empty()){const auto source=required(index,1,"exercise source");exact(source,block.code,true);no_reading(source);}
                if(block.type==BlockType::multiple_choice){for(std::size_t option=0;option<block.options.size();++option){const auto radio=required(index,20+static_cast<int>(option)*2,"answer radio"),text=required(index,21+static_cast<int>(option)*2,"answer option");exact(text,block.options[option],false);require(narrates(text,block.options[option]),"A visible answer option was omitted from narration");require(SendMessageW(radio,BM_GETCHECK,0,0)==BST_UNCHECKED,"A fresh exercise preselected a hidden solution");}}
                else if(block.type==BlockType::inline_fill_code||block.type==BlockType::drag_fill_code){const auto fill=required(index,5,"code hole surface");std::size_t holes=0;for(std::size_t at=0;(at=block.code_template.find("[]",at))!=std::string::npos;at+=2)++holes;
                    for(std::size_t hole=0;hole<holes;++hole)require(child(fill,100+static_cast<int>(hole))!=nullptr,"A template omitted an authored editable/drop hole");require(!child(fill,100+static_cast<int>(holes)),"A template invented an extra answer hole");
                    if(block.type==BlockType::drag_fill_code)for(std::size_t choice=0;choice<block.choices.size();++choice){const auto card=child(fill,2000+static_cast<int>(choice));require(card&&caption(card)==block.choices[choice],"A code drop puzzle changed or omitted an authored choice");}}
                else if(block.type==BlockType::order_code){const auto order=required(index,5,"code-order surface");for(std::size_t fragment=0;fragment<block.fragments.size();++fragment){const auto row=child(order,100+static_cast<int>(fragment));require(row!=nullptr,"A code-order surface omitted an original fragment");exact(row,block.fragments[fragment],true);no_reading(row);}}
                else{const auto answer=required(index,2,"answer field");require(edit_text(answer).empty(),"A fresh response disclosed an answer or retained another activity's draft");require((GetWindowLongPtrW(answer,GWL_STYLE)&ES_READONLY)==0,"A fresh response field is not editable");
                    if(!block.code.empty()){RECT source{},response{},viewport{};GetWindowRect(control(index,1),&source);GetWindowRect(answer,&response);GetWindowRect(pane,&viewport);
                        const auto bounds=[](RECT value){return std::to_string(value.left)+','+std::to_string(value.top)+','+std::to_string(value.right)+','+std::to_string(value.bottom);};
                        require(source.bottom<=response.top,"The independent source and response cards overlap at block"+std::to_string(index)+
                            "; source="+bounds(source)+"; response="+bounds(response)+"; viewport="+bounds(viewport)+"; source_lines="+std::to_string(std::ranges::count(block.code,'\n')+1));}}}
        }
        const auto feedback=control(index,4);if(feedback){require(edit_text(feedback).empty(),"An unanswered exercise exposed feedback before its attempt");no_reading(feedback);}
    }
}
}
int main(int argc,char** argv){try{
    std::cout<<std::unitbuf;
    require(argc==4,"Provide locale, partition index and partition count");const std::string locale=argv[1];require(locale=="es"||locale=="en"||locale=="fr","Unexpected composition locale");
    const auto partition=static_cast<std::size_t>(std::stoul(argv[2])),partitions=static_cast<std::size_t>(std::stoul(argv[3]));
    require(partitions==16&&partition<partitions,"Unexpected census partition contract");
    wchar_t trace_flag[8]{};const bool trace=GetEnvironmentVariableW(L"LGAED_COMPOSITION_TRACE",trace_flag,8)==1&&trace_flag[0]==L'1';
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);winrt::init_apartment(winrt::apartment_type::single_threaded);reader_theme::prepare_fonts();INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES|ICC_PROGRESS_CLASS};InitCommonControlsEx(&common);
    stdin_metadata_contract();
    const auto environment=resolve_windows_environment({ProfileId::Codex,L"composition-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false});OwnedQa cleanup(environment.paths.root);initialize_profile_directories(environment.paths);
    state::StateStore store(environment.paths.root);auto initial=store.load();initial.preferences["native_windows"]["ui_language"]=locale;store.save(initial);
    Window host{CreateWindowExW(WS_EX_CONTROLPARENT,L"STATIC",L"Published curriculum composition QA",WS_OVERLAPPEDWINDOW,0,0,1100,850,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)};require(host.value!=nullptr,"Cannot create isolated composition QA host");
    const auto root=std::filesystem::path(build::source_root)/"app/src/main/assets/content";Counts counts;std::unique_ptr<windows::NarrationWorker> worker;
    LessonViewHooks hooks;hooks.math_editor_request=[&](std::string_view request){if(!worker)worker=std::make_unique<windows::NarrationWorker>(std::filesystem::path(build::source_root)/"out/narration/runtime.json");return worker->math_editor(request);};
    const auto entries=published(root,locale);
    struct Authored{PublishedLesson entry;curriculum::Lesson model;};std::vector<Authored> models;models.reserve(entries.size());
    std::vector<std::pair<std::size_t,std::size_t>> pages;std::set<std::string> plan_routes;std::set<BlockType> plan_types;std::string frame="published-composition-v1/"+locale;
    std::size_t planned_blocks=0;
    for(const auto& entry:entries){auto model=windows::load_lesson(entry.path,entry.locale);plan_routes.insert(entry.route);
        const auto relative=entry.path.lexically_relative(root).generic_u8string();frame+='\n';frame.append(reinterpret_cast<const char*>(relative.data()),relative.size());frame+='\n'+state::sha256(model.raw_json);
        for(std::size_t page=0;page<model.pages.size();++page){pages.emplace_back(models.size(),page);frame+='\n'+model.pages[page].stable_id+':'+std::to_string(model.pages[page].blocks.size());
            for(const auto& block:model.pages[page].blocks){plan_types.insert(block.type);++planned_blocks;}}
        models.push_back({entry,std::move(model)});
    }
    require(plan_routes.contains("programming")&&plan_routes.contains("math_physics")&&plan_types.contains(BlockType::cpp_compiler)&&plan_types.contains(BlockType::math_puzzle),"The complete census plan omitted a learning route or container family");
    require(pages.size()>=partitions,"Published content no longer fills the declared census partitions");
    // Verify the exact union and disjointness before constructing this slice.
    // Every invocation derives the same plan directly from the published assets;
    // the fingerprint lets the central test run detect a changed curriculum.
    std::vector<unsigned char> covered(pages.size(),0);std::size_t union_blocks=0;
    for(std::size_t slice=0;slice<partitions;++slice){const auto first=pages.size()*slice/partitions,last=pages.size()*(slice+1)/partitions;
        for(auto at=first;at<last;++at){require(++covered[at]==1,"Census partitions overlap a published page");const auto [owner,page]=pages[at];union_blocks+=models[owner].model.pages[page].blocks.size();}}
    require(std::ranges::all_of(covered,[](auto count){return count==1;})&&union_blocks==planned_blocks,"Census partitions omit a published page or block");
    const auto first=pages.size()*partition/partitions,last=pages.size()*(partition+1)/partitions;std::size_t expected_blocks=0;std::set<std::size_t> seen_lessons;std::map<std::string,std::pair<std::size_t,std::size_t>> route_counts;
    std::cout<<"PLAN "<<locale<<" sha256="<<state::sha256(frame)<<" lessons="<<models.size()<<" pages="<<pages.size()<<" blocks="<<planned_blocks<<"; partition="<<partition<<'/'<<partitions<<" range=["<<first<<','<<last<<")\n";
    std::string current_course;std::size_t current_lesson=std::numeric_limits<std::size_t>::max();
    for(auto at=first;at<last;++at){const auto [owner,page]=pages[at];const auto& entry=models[owner].entry;const auto& original=models[owner].model;const auto scope=entry.route+'/'+entry.course;
        if(scope!=current_course){current_course=scope;std::cout<<"CENSUS "<<locale<<' '<<scope<<"; constructed pages="<<counts.pages<<'\n';}
        if(seen_lessons.insert(owner).second)++counts.lessons;counts.routes.insert(entry.route);counts.asset_locales.insert(entry.locale);
        if(trace&&current_lesson!=owner)std::cout<<"START "<<entry.path.string()<<'\n';current_lesson=owner;
        expected_blocks+=original.pages[page].blocks.size();
        const auto page_started=GetTickCount64();if(trace)std::cout<<"BEGIN page "<<page+1<<'/'<<original.pages.size()<<" blocks="<<original.pages[page].blocks.size()<<'\n';
            // This projection ONLY constructs the authored page's presentation.
            // Original blocks, text, answers, files and metadata are copied intact.
            // The full parsed model above validates reference/dependency metadata;
            // access gates and imported state are tested by native.lesson_view.
            // No solution/receipt is fabricated and no QA response is submitted.
            auto model=original;model.pages={original.pages[page]};model.state_layout={};model.reference_after.clear();model.activity_after.clear();
            try{Window view{create_lesson_view(host.value,environment,model,hooks)};require(view.value!=nullptr,"Cannot construct the published page");MoveWindow(view.value,0,0,1000,750,FALSE);assert_page(view.value,model,locale,counts);++counts.pages;
                require(store.load()==initial,"Constructing a page modified the isolated persisted student state without an edit");
            }catch(const winrt::hresult_error& error){throw std::runtime_error(entry.path.string()+":page"+std::to_string(page+1)+": "+winrt::to_string(error.message()));}
            catch(const std::exception& error){throw std::runtime_error(entry.path.string()+":page"+std::to_string(page+1)+": "+error.what());}
        ++route_counts[entry.route].first;route_counts[entry.route].second+=original.pages[page].blocks.size();
        const auto elapsed=GetTickCount64()-page_started;if(trace)std::cout<<"END page "<<page+1<<" in "<<elapsed<<"ms\n";
        else if(elapsed>1500)std::cout<<"CENSUS slow "<<entry.path.filename().string()<<" page "<<page+1<<": "<<elapsed<<"ms\n";
    }
    require(counts.pages==last-first&&counts.blocks==expected_blocks,"The selected census partition did not construct every assigned page/block");
    std::cout<<"PASS published "<<locale<<" partition "<<partition<<'/'<<partitions<<": "<<counts.lessons<<" lessons / "<<counts.pages<<" real LessonView pages / "<<counts.blocks<<" blocks; projects="<<counts.projects<<", CMake="<<counts.cmake<<", stdin="<<counts.stdin_fields<<", output checks="<<counts.output_checks<<", math containers="<<counts.math_containers<<", verified math projections="<<counts.math_projections<<'\n';
    for(const auto& [route,total]:route_counts)std::cout<<"COVERAGE "<<route<<": "<<total.first<<" pages / "<<total.second<<" blocks\n";
    for(const auto& [type,count]:counts.types)std::cout<<name(type)<<'='<<count<<' ';std::cout<<"\nConstruction checks original text, controls, roles and narration exclusions; it does not establish solutions, access gates, visual equivalence or audible narration. The dedicated mathematics suite checks every Spanish response composition.\n";
    return 0;
}catch(const winrt::hresult_error& error){std::cerr<<"FAIL composition: "<<winrt::to_string(error.message())<<'\n';}catch(const std::exception& error){std::cerr<<"FAIL composition: "<<error.what()<<'\n';}return 1;}
