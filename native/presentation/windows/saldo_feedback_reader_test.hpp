#pragma once
// The target activity and its heading are copied from the active curriculum.
// Playback, answers and bookmarks belong only to a new hidden QA view.
namespace saldo_feedback_reader_test {
const std::filesystem::path published_asset=std::filesystem::path(build::source_root)/
    "app/src/main/assets/content/es/programming/cpp_fundamentals/edition_es_1__expresiones_y_decisiones/lesson_alternativas_y_fronteras.json";
void cold_play(const std::shared_ptr<windows::NarrationWorker>& backend,double rate,std::string_view voice="ef_dora"){
    const auto runtime=std::filesystem::path(build::source_root)/"out/narration/runtime.json";
    const auto catalog=windows::kokoro_voice_catalog(runtime);
    check(std::ranges::any_of(catalog,[&](const auto& value){return value.id==voice&&value.language=="es";}),"Cold-play voice is not an installed-catalog Spanish identity");
    const auto root=std::filesystem::path(L"C:/LGAED-QA")/(L"published-cold-play-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    const auto content=std::filesystem::path(build::source_root)/"app/src/main/assets/content";
    const auto copy=root/L"content"/published_asset.lexically_relative(content);std::filesystem::create_directories(copy.parent_path());
    for(const auto& source:{published_asset,published_asset.parent_path().parent_path()/L"course_cpp_fundamentals.json",
        published_asset.parent_path()/(L"unit_"+published_asset.parent_path().filename().native()+L".json")}){
        const auto destination=root/L"content"/source.lexically_relative(content);std::filesystem::create_directories(destination.parent_path());
        std::filesystem::copy_file(source,destination);
    }
    const auto original=state::read_bytes(published_asset);const auto lesson=windows::load_lesson(copy);
    const std::array<std::string,7> sentences{
        "En programación, un bloque agrupa instrucciones que el programa trata como una unidad.",
        "En este ejemplo de C++, cada bloque empieza con una llave de apertura { y termina con la llave de cierre } que le corresponde.",
        "La { que sigue a int main() abre el cuerpo de la función main.",
        "Dentro de ese cuerpo, la { que sigue a if (pedidos > 0) abre otro bloque.",
        "El bloque del if está anidado dentro del de main.","La primera } cierra el cuerpo del if.","La última } cierra el cuerpo de main."};
    const auto initialize=[&](state::StateData& data){data.preferences["native_windows"]["ui_language"]=std::string("es");
        for(const auto& block:lesson.pages.front().blocks)if(block.type==curriculum::BlockType::multiple_choice){
            check(block.correct_index&&curriculum::evaluate_choice(block,*block.correct_index).correct,"Invalid prior-page control answer");
            state::Response response;response.selected=static_cast<std::int64_t>(*block.correct_index);response.answer=block.options[*block.correct_index];response.correct=true;
            state::mark_response_reviewed(response,block);state::put_response(data,lesson,block,response);}
        check(state::mark_page_reached(data,lesson,0),"Cannot admit the isolated current page");state::save_current_page(data,lesson,1);
    };
    Fixture f(backend,false,true,false,"es",lesson,true,0,true,false,[&](HWND view){
        update_lesson_preferences(view,[&](state::StateData& data){data.preferences["lesson_narration_settings"]["rate"]=rate;
            data.preferences["lesson_narration_settings"]["kokoro_voice:es"]=std::string(voice);});
    },initialize,root/L"audio",true);
    f.reader->set_qa_playback_muted(true);
    const auto verify_voice=[&]{const auto data=read_lesson_state(f.view);
        check(state::string_value(data,"lesson_narration_settings","kokoro_voice:es")==voice&&windows::selected_kokoro_voice(data,"es",runtime)==voice,
            "Cold reader preferences or the catalog resolver selected a different Spanish voice");};
    verify_voice();
    const auto started=GetTickCount64();std::vector<std::size_t> trace;bool first_available=false,caption_played=false,caption_before_complete=false;
    bool next_prose=false,prose_before_complete=false;ULONGLONG first_available_ms=0,caption_ms=0,prose_ms=0,last_observed=0;
    std::optional<std::size_t> last_index;std::size_t last_available=std::numeric_limits<std::size_t>::max();
    std::cout<<"COLD_BEGIN rate="<<rate<<" voice="<<voice<<" root="<<root.string()<<" complete_barrier=false\n";
    f.reader->toggle(); // Intentionally precedes both planning and complete-page audio.
    const auto observe=[&](std::string_view phase){const auto value=f.reader->snapshot();const auto now=GetTickCount64();
        const auto available=std::ranges::count_if(value.segments,[](const auto& part){return part.available;});
        if(!last_index||*last_index!=value.index||last_available!=static_cast<std::size_t>(available)||now-last_observed>2000){
            std::cout<<"COLD_STATE phase="<<phase<<" elapsed_ms="<<now-started<<" index="<<value.index<<" offset="<<value.offset
                <<" planned="<<value.planned<<" preparing="<<value.preparing<<" allocated="<<value.allocated<<" playing="<<value.playing
                <<" waiting="<<value.waiting_gate<<" suspended="<<value.preparation_suspended<<" verifying="<<value.waiting_verification
                <<" available="<<available<<'/'<<value.segments.size()<<" complete="<<value.page_audio_complete<<" error="<<value.error;
            if(value.index<value.segments.size())std::cout<<" current_available="<<value.segments[value.index].available<<" source="<<value.segments[value.index].source;
            std::cout<<'\n';last_index=value.index;last_available=static_cast<std::size_t>(available);last_observed=now;
        }
        check(value.error.empty(),"Cold current-page reader failed: "+value.error);return value;
    };
    wait([&]{const auto value=observe("cold");if(!value.planned||value.segments.empty())return false;
        check(lesson_page_state(f.view).index==1,"Cold playback opened another page");
        if(!first_available&&value.segments.front().available){first_available=true;first_available_ms=GetTickCount64()-started;
            check(!value.page_audio_complete,"The cold regression reached an accidental whole-page preparation barrier");}
        if(value.playing){if(trace.empty()||trace.back()!=value.index)trace.push_back(value.index);
            const auto& current=value.segments[value.index];check(current.block<5&&!current.feedback,"Cold prefix crossed the first unanswered exercise");
            if(current.block==2&&current.source=="Ejemplo de código C más más."){caption_played=true;caption_before_complete=!value.page_audio_complete;caption_ms=GetTickCount64()-started;}
            if(caption_played&&current.block==3&&value.offset>=120){next_prose=true;prose_before_complete=!value.page_audio_complete;prose_ms=GetTickCount64()-started;return true;}
        }
        return false;
    },"cold real paragraph, passive C++ caption and next prose before full generation",240000);
    f.reader->pause();const auto paused=f.snapshot();check(first_available&&caption_played&&caption_before_complete&&next_prose&&prose_before_complete,
        "Cold narration waited for the complete canonical tail before its C++ caption and next paragraph");
    std::vector<std::string> paragraph;for(const auto index:trace)if(paused.segments[index].block==1)paragraph.push_back(paused.segments[index].source);
    check(std::ranges::equal(paragraph,sentences),"Cold playback skipped or repeated one of the seven actual paragraph sentences");
    for(const auto& source:sentences){const auto part=std::ranges::find(paused.segments,source,&LessonReaderSegmentSnapshot::source);
        check(part!=paused.segments.end()&&part->available,"An actual prefix sentence has no available recording");(void)verify_feedback_recording(*part);}
    f.reader.reset();check(flush_lesson_view(f.view),"Cannot save the early real playback pause");DestroyWindow(f.view);
    f.view=create_lesson_view(f.host,f.environment,windows::load_lesson(copy));check(f.view!=nullptr,"Cannot reopen the unchanged exact QA source");MoveWindow(f.view,0,0,980,720,TRUE);
    LessonViewHooks hooks;hooks.before_page_change=[&](HWND){if(f.reader)f.reader->pause();};hooks.content_changed=[&](HWND){if(f.reader)f.reader->content_changed();};
    hooks.feedback_changed=[&](HWND,std::size_t block,bool correct){if(f.reader)f.reader->feedback(block,correct);};
    hooks.validate_math=[backend](std::string_view block,std::string_view answers,std::string_view locale,std::stop_token stop){return backend->validate_math(block,answers,locale,stop);};
    set_lesson_view_hooks(f.view,std::move(hooks));f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});f.reader->set_qa_playback_muted(true);
    verify_voice();last_index.reset();f.reader->toggle();
    wait([&]{const auto value=observe("reopen");if(!value.planned)return false;
        check(value.key==paused.key&&value.index>=paused.index,"Immediate reopen replayed earlier content instead of its exact real pause");
        if(value.index==paused.index)check(value.offset>=paused.offset,"Immediate reopen moved behind its exact saved offset");
        return value.playing&&value.index>paused.index&&value.segments[value.index].block==3;
    },"immediate early-bookmark resume without a full-page preparation barrier",240000);
    f.reader->pause();check(state::read_bytes(copy)==original&&state::read_bytes(published_asset)==original,"Cold playback changed curricular source bytes");
    std::cout<<"PASS published cold playback "<<rate<<"X voice="<<voice<<": seven actual semantic sentences in order, passive code caption then following prose while canonical tail was incomplete; first_available_ms="
        <<first_available_ms<<" caption_ms="<<caption_ms<<" prose_ms="<<prose_ms<<"; exact early resume, no whole-page readiness barrier, real instance-muted Kokoro/Windows playback\n";
}
void published_page(const std::shared_ptr<windows::NarrationWorker>& backend,double rate,
                    std::string_view voice="ef_dora",bool streaming=false,bool live_revision=false) {
    const auto original=state::read_bytes(published_asset);
    const auto root=std::filesystem::path(L"C:/LGAED-QA")/(L"saldo-published-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    const auto content=std::filesystem::path(build::source_root)/"app/src/main/assets/content";
    const auto copy=root/L"content"/published_asset.lexically_relative(content);std::filesystem::create_directories(copy.parent_path());
    {std::ofstream stream(copy,std::ios::binary);stream.write(reinterpret_cast<const char*>(original.data()),static_cast<std::streamsize>(original.size()));
        check(static_cast<bool>(stream),"Cannot write the literal isolated curriculum copy");}
    // Course-edition loading authenticates the physical language/course/unit
    // route and its active indices. Copy those exact descriptors alongside the
    // exact lesson; neither relax the loader nor strip the publication metadata.
    const auto course=published_asset.parent_path().parent_path()/L"course_cpp_fundamentals.json";
    const auto unit=published_asset.parent_path()/(L"unit_"+published_asset.parent_path().filename().native()+L".json");
    for(const auto& index:{course,unit}){
        const auto destination=root/L"content"/index.lexically_relative(content);
        std::filesystem::create_directories(destination.parent_path());std::filesystem::copy_file(index,destination);
    }
    const auto lesson=windows::load_lesson(copy);const auto& page=lesson.pages.at(1);
    const auto goal=std::ranges::find_if(page.blocks,[](const auto& block){return block.type==curriculum::BlockType::fill_code&&block.answers==std::vector<std::string>{"saldo"};});
    check(goal!=page.blocks.end()&&goal+1<page.blocks.end()&&(goal+1)->text=="Usar el valor después del bloque","Published saldo source changed");
    const auto saldo=static_cast<std::size_t>(goal-page.blocks.begin());
    // This prefix only admits page two in the isolated profile. Current-page
    // answers and every listening certificate are earned below by real actions.
    const auto prefix=[&](state::StateData& data){
        data.preferences["native_windows"]["ui_language"]=std::string("es");
        for(const auto& block:lesson.pages.front().blocks){if(block.type!=curriculum::BlockType::multiple_choice)continue;
            check(block.correct_index&&curriculum::evaluate_choice(block,*block.correct_index).correct,"Invalid preceding-page choice");
            state::Response response;response.selected=static_cast<std::int64_t>(*block.correct_index);response.answer=block.options[*block.correct_index];response.correct=true;
            state::mark_response_reviewed(response,block);state::put_response(data,lesson,block,response);}
        check(state::mark_page_reached(data,lesson,0),"Cannot visit the isolated preceding page");state::save_current_page(data,lesson,1);
    };
    Fixture f(backend,false,true,true,"es",lesson,true,0,true,false,[&](HWND view){
        update_lesson_preferences(view,[&](state::StateData& data){data.preferences["lesson_narration_settings"]["rate"]=rate;
            data.preferences["lesson_narration_settings"]["kokoro_voice:es"]=std::string(voice);});
    },prefix,root/L"audio",true);
    f.reader->set_qa_playback_muted(true);
    wait([&]{const auto value=f.snapshot();return lesson_page_state(f.view).index==1&&value.planned&&!value.page_audio_revision.empty();},"complete authentic page plan",180000);
    if(!streaming)wait([&]{return f.snapshot().page_audio_complete;},"fully generated authentic page before its real answers",900000);
    else check(!f.snapshot().page_audio_complete,"Streaming published regression accidentally waited for complete page audio before answering");
    check(windows::selected_kokoro_voice(read_lesson_state(f.view),"es",std::filesystem::path(build::source_root)/"out/narration/runtime.json")==voice,
        "Published feedback regression selected a different installed voice");
    std::size_t answered=0;bool math_answered=false;
    const auto submit=[&](std::size_t index){const auto& block=page.blocks[index];
        if(block.type==curriculum::BlockType::fill_code||block.type==curriculum::BlockType::output_prediction){
            const auto edit=child(f.view,1002+static_cast<int>(index)*100);const auto text=wide(block.answers.at(0));CHARRANGE all{0,-1};
            SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&all));SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(text.c_str()));
        }else if(block.type==curriculum::BlockType::multiple_choice){
            check(block.correct_index.has_value(),"Published choice lost its answer");const auto toggle=child(f.view,1020+static_cast<int>(index)*100+static_cast<int>(*block.correct_index)*2);
            SendMessageW(f.view,WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(toggle),BN_CLICKED),reinterpret_cast<LPARAM>(toggle));
        }else{
            check(block.type==curriculum::BlockType::math_puzzle,"Unexpected activity before saldo");
            using namespace winrt::Windows::Data::Json;const auto specification=JsonObject::Parse(wide(block.raw_json));
            const auto response=specification.GetNamedArray(L"responses").GetObjectAt(0);const auto options=response.GetNamedArray(L"options");
            const auto expected=response.GetNamedObject(L"validator").GetNamedArray(L"expected").GetStringAt(0);std::size_t option=options.Size();
            for(unsigned n=0;n<options.Size();++n)if(options.GetObjectAt(n).GetNamedString(L"id")==expected){option=n;break;}
            std::vector<HWND> radios;EnumChildWindows(child(f.view,1005+static_cast<int>(index)*100),[](HWND item,LPARAM data)->BOOL{
                wchar_t name[32]{};GetClassNameW(item,name,32);if(std::wstring_view(name)==L"Button"&&(GetWindowLongPtrW(item,GWL_STYLE)&BS_TYPEMASK)==BS_RADIOBUTTON)
                    reinterpret_cast<std::vector<HWND>*>(data)->push_back(item);return TRUE;},reinterpret_cast<LPARAM>(&radios));
            std::ranges::sort(radios,{},[](HWND item){return GetDlgCtrlID(item);});check(radios.size()==options.Size()&&option<radios.size(),"Cannot identify the real math choice radios");
            const auto toggle=radios[option];SendMessageW(GetParent(toggle),WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(toggle),BN_CLICKED),reinterpret_cast<LPARAM>(toggle));math_answered=true;
        }
        const auto action=child(f.view,1003+static_cast<int>(index)*100);SendMessageW(action,BM_CLICK,0,0);
    };
    for(std::size_t index=0;index<=saldo;++index){const auto type=page.blocks[index].type;
        if(type!=curriculum::BlockType::fill_code&&type!=curriculum::BlockType::output_prediction&&type!=curriculum::BlockType::multiple_choice&&type!=curriculum::BlockType::math_puzzle)continue;
        f.reader->pause();submit(index);
        wait([&]{const auto value=f.snapshot();return value.planned&&value.segments[value.index].block==index&&value.segments[value.index].feedback&&value.segments[value.index].correct;},"real published correct answer",180000);
        f.reader->toggle();bool actually_played=false;std::optional<std::size_t> observed_index;
        wait([&]{const auto value=f.snapshot();if(value.playing&&value.segments[value.index].block==index&&value.segments[value.index].feedback)actually_played=true;
            if(streaming&&(!observed_index||*observed_index!=value.index)){
                observed_index=value.index;const auto& part=value.segments[value.index];
                std::cout<<"SALDO_STREAM phase=feedback goal="<<index<<" index="<<value.index<<" block="<<part.block
                    <<" playing="<<value.playing<<" waiting="<<value.waiting_gate<<" preparing="<<value.preparing
                    <<" complete="<<value.page_audio_complete<<" heard="<<value.heard_through<<" source="<<part.source<<'\n';}
            if(!actually_played||!value.playing||value.segments[value.index].block<=index)return false;
            for(const auto& part:value.segments)if(part.block==index&&part.feedback&&part.correct)
                check(part.feedback_fully_played,"Published feedback continued without hearing every actual PCM sentence");
            check(value.playback_rate==rate,"Published feedback continuation changed speed");return true;
        },"published correct feedback automatically enters its next actual content",240000);
        if(streaming&&index==saldo){const auto heading=f.snapshot();
            check(heading.segments[heading.index].block==saldo+1&&heading.segments[heading.index].source=="Usar el valor después del bloque",
                "Streaming saldo feedback skipped its immediate authentic heading");
            std::cout<<"SALDO_STREAM phase=heading complete="<<heading.page_audio_complete<<" index="<<heading.index<<'\n';
            wait([&]{const auto value=f.snapshot();if(!value.playing||value.segments[value.index].block!=saldo+2||value.offset<80)return false;
                check(value.segments[value.index].source.starts_with("Una tienda da puntos extra"),"Saldo continuation reached the wrong prose");
                std::cout<<"SALDO_STREAM phase=following-prose complete="<<value.page_audio_complete<<" index="<<value.index<<" offset="<<value.offset<<'\n';
                (void)verify_feedback_recording(value.segments[value.index]);return true;
            },"streaming authentic heading continues to its immediately following prose",180000);
        }
        f.reader->pause();const auto value=f.snapshot();
        for(const auto& part:value.segments)if(part.block==index&&part.feedback&&part.correct)(void)verify_feedback_recording(part);
        ++answered;
        std::cout<<"SALDO_PUBLISHED_FEEDBACK rate="<<rate<<" block="<<index<<" next_block="<<value.segments[value.index].block<<" heard_through="<<value.heard_through<<'\n';
        if(index==saldo&&!streaming)check(value.segments[value.index].block==saldo+1&&value.segments[value.index].source=="Usar el valor después del bloque",
            "Published saldo feedback failed to play its immediate authentic heading");
    }
    check(math_answered&&answered>=6&&state::read_bytes(published_asset)==original&&state::read_bytes(copy)==original,
        "The authentic regression skipped a prerequisite/math or altered curriculum");
    if(live_revision){
        using namespace winrt::Windows::Data::Json;
        check(streaming,"Live published regression requires immediate streaming actions");
        const auto before=f.snapshot();const auto before_state=read_lesson_state(f.view);
        const auto before_revision=lesson_page_state(f.view).content_revision;
        std::map<std::filesystem::path,std::string> recordings;
        for(const auto& part:before.segments)if(part.available&&!part.recording.empty())recordings.try_emplace(part.recording,state::sha256(state::read_bytes(part.recording)));
        // The QA author revises the next prose and places it before the title.
        // Both physical blocks and the explicit ID plan receive that same move;
        // all original contracts, answers, metadata and semantic spans remain.
        auto source=JsonObject::Parse(wide(lesson.raw_json));
        auto blocks=source.GetNamedArray(L"pages").GetObjectAt(1).GetNamedArray(L"blocks");
        auto layout=source.GetNamedObject(L"stateLayout");auto ids=layout.GetNamedArray(L"current").GetObjectAt(1).GetNamedArray(L"blocks");
        const auto title=blocks.GetAt(static_cast<unsigned>(saldo+1));
        const auto body=blocks.GetObjectAt(static_cast<unsigned>(saldo+2));
        const auto title_id=ids.GetAt(static_cast<unsigned>(saldo+1));const auto body_id=ids.GetAt(static_cast<unsigned>(saldo+2));
        const auto earlier=test::compile_check_test_fingerprint(body);
        const std::string added_sentence="Esta frase nueva pertenece únicamente a la revisión local de prueba.";
        const auto revised_text=utf8(body.GetNamedString(L"text"))+" "+added_sentence;
        body.Insert(L"text",JsonValue::CreateStringValue(wide(revised_text)));
        JsonObject proof;proof.Insert(L"id",body_id);
        proof.Insert(L"beforeSha256",JsonValue::CreateStringValue(wide(earlier)));
        proof.Insert(L"afterSha256",JsonValue::CreateStringValue(wide(test::compile_check_test_fingerprint(body))));
        layout.GetNamedArray(L"editorial").Append(proof);
        for(const auto& value:source.GetNamedObject(L"textSemantics").GetNamedArray(L"fields")){
            auto field=value.GetObject();if(field.GetNamedString(L"block")==body_id.GetString()&&field.GetNamedString(L"path")==L"/text")
                field.Insert(L"sourceSha256",JsonValue::CreateStringValue(wide(state::sha256(revised_text))));}
        blocks.SetAt(static_cast<unsigned>(saldo+1),body);blocks.SetAt(static_cast<unsigned>(saldo+2),title);
        ids.SetAt(static_cast<unsigned>(saldo+1),body_id);ids.SetAt(static_cast<unsigned>(saldo+2),title_id);
        const auto bytes=utf8(source.Stringify());const auto revised=windows::parse_lesson_json(bytes);
        check(revised.pages[1].blocks[saldo+1].stable_id==page.blocks[saldo+2].stable_id&&
            revised.pages[1].blocks[saldo+2].stable_id==page.blocks[saldo+1].stable_id,
            "The authored live publication did not preserve the moved block identities");
        const auto pending=root/L"live-publication.json";
        {std::ofstream stream(pending,std::ios::binary);stream<<bytes;check(static_cast<bool>(stream),"Cannot stage the isolated live publication");}
        check(MoveFileExW(pending.c_str(),copy.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE,
            "Cannot publish the isolated live revision atomically");
        wait([&]{const auto value=f.snapshot();check(!value.playing&&!value.allocated,"Live publication overrode the explicit pause");
            return lesson_page_state(f.view).content_revision>before_revision&&value.planned&&!value.waiting_verification&&value.key!=before.key;
        },"same hidden view automatically adopts the authenticated prose revision and order",180000);
        const auto adopted=f.snapshot();
        check(adopted.segments[adopted.index].block==saldo+1&&!adopted.segments[adopted.index].feedback&&adopted.offset==0&&
            adopted.segments[adopted.index].source.starts_with("Una tienda da puntos extra")&&adopted.heard_through<=static_cast<std::int64_t>(adopted.index),
            "Live revision retained the old order or marked the new prose as already heard");
        for(const auto& block:page.blocks)if(const auto saved=state::get_response(before_state,lesson,block);saved&&saved->correct){
            const auto current=state::get_response(read_lesson_state(f.view),lesson,block);
            check(current&&current->correct&&current->answer==saved->answer&&current->selected==saved->selected&&current->raw_json==saved->raw_json,
                "Live prose/order publication changed a previously checked response");}
        std::vector<std::size_t> expected_prose,played_prose;
        for(std::size_t n=0;n<adopted.segments.size();++n)if(adopted.segments[n].block==saldo+1&&!adopted.segments[n].feedback)
            expected_prose.push_back(n);
        check(!expected_prose.empty()&&std::ranges::any_of(expected_prose,[&](auto n){return adopted.segments[n].source==added_sentence;}),
            "The revised live plan omitted the newly authored sentence");
        f.reader->toggle();bool added_sentence_played=false;
        wait([&]{const auto value=f.snapshot();if(!value.playing)return false;
            const auto& part=value.segments[value.index];
            if(part.block==saldo+1&&value.offset>80&&(played_prose.empty()||played_prose.back()!=value.index)){
                played_prose.push_back(value.index);(void)verify_feedback_recording(part);
                added_sentence_played=added_sentence_played||part.source==added_sentence;
                std::cout<<"SALDO_LIVE_PLAYED index="<<value.index<<" offset="<<value.offset<<" source="<<part.source<<'\n';
            }
            if(part.block!=saldo+2)return false;
            check(played_prose==expected_prose&&added_sentence_played&&part.source=="Usar el valor después del bloque",
                "Explicit Play skipped a revised/new sentence, repeated prose or retained the obsolete title position");
            (void)verify_feedback_recording(part);return value.offset>80;
        },"explicit Play reads the revised prose followed by the title in the actual new order",240000);
        f.reader->pause();for(const auto& [file,checksum]:recordings)check(std::filesystem::is_regular_file(file)&&state::sha256(state::read_bytes(file))==checksum,
            "Live publication deleted or overwrote a previous actual recording");
        check(state::read_bytes(published_asset)==original,"Live QA changed the original published curriculum");
        std::cout<<"PASS published live prose/order revision: same view timer, intact IDs/responses/old PCM, honest pending prose, preserved pause and explicit Play in the new order\n";
    }
    auto paused=f.snapshot();const auto saved=read_lesson_state(f.view);
    for(const bool manual_pause:{false,true}){
        f.reader.reset();check(flush_lesson_view(f.view),"Cannot flush authentic QA state");
        const auto before=narration::read_history(read_lesson_state(f.view));DestroyWindow(f.view);f.view=create_lesson_view(f.host,f.environment,windows::load_lesson(copy));
        check(f.view!=nullptr,"Cannot reopen the exact authentic QA publication");MoveWindow(f.view,0,0,980,720,TRUE);
        struct RestoreGate{std::mutex mutex;std::condition_variable_any changed;bool allow=false;std::atomic<bool> evaluated=false;};
        const auto gate=std::make_shared<RestoreGate>();LessonViewHooks hooks;
        hooks.before_page_change=[&](HWND){if(f.reader)f.reader->pause();};hooks.content_changed=[&](HWND){if(f.reader)f.reader->content_changed();};
        hooks.feedback_changed=[&](HWND,std::size_t block,bool correct){if(f.reader)f.reader->feedback(block,correct);};
        hooks.validate_math=[backend,gate](std::string_view block,std::string_view answers,std::string_view locale,std::stop_token stop){
            auto reply=backend->validate_math(block,answers,locale,stop);gate->evaluated.store(true);
            std::unique_lock lock(gate->mutex);if(!gate->changed.wait(lock,stop,[&]{return gate->allow;}))throw std::runtime_error("QA restoration cancelled");
            return reply;
        };
        set_lesson_view_hooks(f.view,std::move(hooks));f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});f.reader->set_qa_playback_muted(true);
        wait([&]{return gate->evaluated.load()&&f.snapshot().waiting_verification;},"real math reply held before restoration",180000);
        f.reader->toggle();const auto requested=f.snapshot();
        check(requested.waiting_verification&&!requested.playing&&!requested.allocated&&narration::read_history(read_lesson_state(f.view))==before,
            "Play during internal verification moved the authentic bookmark or started an incomplete script");
        if(manual_pause)f.reader->pause();
        {const std::lock_guard lock(gate->mutex);gate->allow=true;}gate->changed.notify_all();
        wait([&]{const auto value=f.snapshot();return !value.waiting_verification&&value.planned&&value.key==paused.key&&value.index==paused.index&&
            value.offset>=paused.offset&&value.segments[value.index].available&&(manual_pause||value.playing);},
            "deferred Play resumes the exact authentic position after real math verification",180000);
        const auto restored=f.snapshot();check(restored.heard_through==paused.heard_through,
            "Internal verification discarded earlier real feedback hearing");
        if(manual_pause)check(!restored.playing&&!restored.allocated&&restored.offset==paused.offset,
            "Internal verification overrode a voluntary pause");
        f.reader->pause();paused=f.snapshot();
        std::cout<<"SALDO_PUBLISHED_RESTORE rate="<<rate<<" play_while_verifying=true manual_pause="<<manual_pause<<" resumed="<<!manual_pause<<'\n';
    }
    for(const auto& block:page.blocks)if(const auto before=state::get_response(saved,lesson,block);before&&before->correct){
        const auto after=state::get_response(read_lesson_state(f.view),lesson,block);
        check(after&&after->correct&&after->answer==before->answer&&after->raw_json==before->raw_json&&after->selected==before->selected,
            "Reopening authentic page changed a correctly checked answer");}
    f.reader->toggle();wait([&]{const auto value=f.snapshot();check(!value.planned||value.index>=paused.index,"Reopening repeated previously heard feedback before saldo's heading");
        return value.waiting_gate&&!value.playing;},"reopened authentic heading/prose reaches its next legitimate pending exercise",180000);f.reader->pause();
    const auto coupon_position=curriculum::stable_block_position(lesson,"u3l4_alcance_predecir_cupon");
    check(coupon_position&&coupon_position->page==1,"The reported coupon activity lost its published identity");
    const auto coupon=coupon_position->block;
    check(page.blocks[coupon].type==curriculum::BlockType::output_prediction&&page.blocks[coupon].answers==std::vector<std::string>{"6"}&&
        page.blocks[coupon+1].text=="Reparar un contador de tareas"&&page.blocks[coupon+3].type==curriculum::BlockType::cpp_compiler&&lesson.compiler_puzzles_required,
        "The reported coupon feedback/title/compiler sequence changed");
    check(f.snapshot().segments[f.snapshot().index].block==coupon,"Published continuation stopped at a different pending exercise before the coupon");
    submit(coupon);
    wait([&]{const auto value=f.snapshot();return value.planned&&value.segments[value.index].block==coupon&&
        value.segments[value.index].feedback&&value.segments[value.index].correct;},"real correct coupon answer selects its complete current feedback",180000);
    const auto coupon_plan=f.snapshot();std::vector<std::string> expected_instruction;
    for(const auto& part:coupon_plan.segments)if(part.block==coupon+2&&!part.feedback)expected_instruction.push_back(part.source);
    check(!expected_instruction.empty(),"The authentic compiler instruction has no reading plan");
    f.reader->toggle();bool coupon_feedback_played=false,title_played=false;std::optional<std::size_t> observed;
    std::vector<std::string> played_instruction;
    wait([&]{const auto value=f.snapshot();const auto& part=value.segments[value.index];
        if(!observed||*observed!=value.index){observed=value.index;
            std::cout<<"CUPON_PUBLISHED rate="<<rate<<" index="<<value.index<<" block="<<part.block<<" playing="<<value.playing
                <<" waiting="<<value.waiting_gate<<" heard="<<value.heard_through<<" complete="<<value.page_audio_complete<<" source="<<part.source<<'\n';}
        if(value.playing){
            if(part.block==coupon&&part.feedback&&part.correct)coupon_feedback_played=true;
            if(part.block==coupon+1){
                check(coupon_feedback_played&&part.source=="Reparar un contador de tareas","Coupon feedback skipped or failed to enter its immediate authentic title");
                for(const auto& feedback:value.segments)if(feedback.block==coupon&&feedback.feedback&&feedback.correct){
                    check(feedback.feedback_fully_played,"The repair title started before all real coupon feedback was heard");
                    (void)verify_feedback_recording(feedback);}
                title_played=true;
            }
            if(part.block==coupon+2&&(played_instruction.empty()||played_instruction.back()!=part.source)){
                check(title_played,"The compiler instruction skipped its preceding repair title");played_instruction.push_back(part.source);
                (void)verify_feedback_recording(part);}
            check(part.block<=coupon+3,"Unresolved required compiler allowed the reader to enter its following content");
        }
        if(!value.waiting_gate||value.playing||part.block!=coupon+3)return false;
        check(coupon_feedback_played&&title_played&&played_instruction==expected_instruction,
            "Coupon continuation omitted, repeated or reordered part of its title/compiler instruction");return true;
    },"complete coupon feedback automatically reads its title/instruction and waits only at the required compiler",240000);
    const auto coupon_response=state::get_response(read_lesson_state(f.view),lesson,page.blocks[coupon]);
    check(coupon_response&&coupon_response->correct&&coupon_response->answer=="6","Coupon continuation lost its actual checked answer");
    ++answered;f.reader->pause();
    std::cout<<"PASS published coupon feedback→repair title→complete compiler instruction→unresolved required compiler gate; real answer and PCM retained\n";
    std::cout<<"PASS "<<(streaming?"streaming ":"complete ")<<"published saldo page "<<rate<<"X voice="<<voice<<": exact QA source/IDs/semantics, "<<answered<<" real answers including math, non-silent Kokoro PCM, instance-muted Windows playback and automatic immediate heading"<<(streaming?"/prose without a whole-page readiness barrier":"")<<"; root="<<root.string()<<'\n';
}
void run(const std::shared_ptr<windows::NarrationWorker>& backend,double rate) {
    const auto asset=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/edition_es_1__expresiones_y_decisiones/lesson_alternativas_y_fronteras.json";
    const auto original=state::read_bytes(asset);const auto published=windows::load_lesson(asset);
    const auto& source=published.pages.at(1).blocks;
    const auto goal=std::ranges::find_if(source,[](const auto& block){return block.type==curriculum::BlockType::fill_code&&block.answers==std::vector<std::string>{"saldo"};});
    check(goal!=source.end()&&goal+2<source.end()&&(goal+1)->text=="Usar el valor después del bloque",
        "The reported saldo activity or its next heading changed");
    auto lesson=model();lesson.id="qa_saldo_feedback_continuity";lesson.title="Continuidad del lector";lesson.pages.resize(1);
    auto& blocks=lesson.pages.front().blocks;blocks.front().text="La lectura comienza antes del ejercicio.";
    auto first=*goal;first.id=first.stable_id="previous";first.question="Escribe inicio.";first.code.clear();first.answers={"inicio"};
    first.explanation="Esta explicación debe terminar antes de continuar.";first.text_semantics.clear();first.address={0,1,{},{}};blocks.push_back(std::move(first));
    auto target=*goal;target.id=target.stable_id="saldo";target.address={0,2,{},{}};blocks.push_back(std::move(target));
    for(std::size_t n=1;n<=2;++n){auto following=*(goal+n);following.id=following.stable_id="after_saldo_"+std::to_string(n);
        following.address={0,blocks.size(),{},{}};blocks.push_back(std::move(following));}
    auto later=*goal;later.id=later.stable_id="pending_after_saldo";later.question="Escribe siguiente.";later.code.clear();later.answers={"siguiente"};
    later.explanation="El ejercicio posterior conserva su propio bloqueo.";later.text_semantics.clear();later.address={0,blocks.size(),{},{}};blocks.push_back(std::move(later));
    const auto cache=std::filesystem::path(L"C:/LGAED-QA")/(L"saldo-feedback-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    Fixture f(backend,false,true,true,"es",lesson,true,0,true,true,[&](HWND view){
        update_lesson_preferences(view,[&](state::StateData& data){data.preferences["lesson_narration_settings"]["rate"]=rate;});
    },{},cache);
    f.reader->set_qa_playback_muted(true);
    const auto submit=[&](std::size_t block,std::string_view answer){const auto edit=child(f.view,1002+static_cast<int>(block)*100);
        const auto text=wide(answer);CHARRANGE all{0,-1};SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&all));
        SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(text.c_str()));SendMessageW(child(f.view,1003+static_cast<int>(block)*100),BM_CLICK,0,0);};
    f.reader->toggle();
    wait([&]{const auto value=f.snapshot();return value.waiting_gate&&!value.playing&&value.segments[value.index].block==1;},"real initial unanswered gate");
    submit(1,"inicio");
    wait([&]{const auto value=f.snapshot();return value.waiting_gate&&!value.playing&&value.segments[value.index].block==2;},"correct first feedback automatically reaches saldo");
    submit(2,"saldo");bool heading_played=false;std::vector<std::size_t> feedback;
    wait([&]{const auto value=f.snapshot();if(value.planned)for(std::size_t n=0;n<value.segments.size();++n)
            if(value.segments[n].block==2&&value.segments[n].feedback&&value.segments[n].correct&&std::ranges::find(feedback,n)==feedback.end())feedback.push_back(n);
        if(value.playing&&value.segments[value.index].block==3){heading_played=true;check(value.playback_rate==rate,"The following heading changed speed");}
        return value.waiting_gate&&!value.playing&&value.segments[value.index].block==5;
    },"actual saldo feedback, next heading/prose and later pending prompt",120000);
    const auto stopped=f.snapshot();check(heading_played&&!feedback.empty()&&!stopped.finished&&!stopped.allocated,
        "Correct saldo feedback stopped before its heading or bypassed the later gate");
    for(const auto n:feedback){check(stopped.segments[n].feedback_fully_played,"Unheard saldo feedback released the heading");(void)verify_feedback_recording(stopped.segments[n]);}
    const auto saved=state::get_response(read_lesson_state(f.view),lesson,blocks[2]);check(saved&&saved->correct&&saved->answer=="saldo","Saldo answer was not preserved");
    f.reader->pause();submit(5,"siguiente");f.ready();
    check(!f.snapshot().playing&&!f.snapshot().allocated&&f.snapshot().segments[f.snapshot().index].feedback,"A correct answer overrode the voluntary pause");
    const auto paused=f.snapshot();f.reader.reset();f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});f.reader->set_qa_playback_muted(true);f.ready();const auto reopened=f.snapshot();
    check(reopened.index==paused.index&&reopened.offset==paused.offset&&reopened.key==paused.key&&!reopened.playing&&!reopened.allocated,
        "Reopening moved or started the voluntary pause");
    f.reader->toggle();wait([&]{return f.snapshot().finished;},"explicit resume finishes the final activity");
    check(state::read_bytes(asset)==original,"The regression changed actual curricular content");
    std::cout<<"PASS saldo feedback "<<rate<<"X: real non-silent Kokoro PCM with instance-muted MediaPlayer, automatic next heading/prose, pending gate, voluntary pause and exact reopen; hidden QA cache="<<cache.string()<<'\n';
}
void manual_forward_latest_feedback(const std::shared_ptr<windows::NarrationWorker>& backend){
    const auto original=state::read_bytes(published_asset);const auto published=windows::load_lesson(published_asset);
    const auto& source=published.pages.at(1).blocks;
    check(source.at(25).type==curriculum::BlockType::multiple_choice&&source.at(38).answers==std::vector<std::string>{"6"}&&
        source.at(39).text=="Reparar un contador de tareas","The exact reported feedback sources changed");
    for(const int variant:{0,1,2}){
        const bool reopen=variant==1,legacy=variant==2;const double rate=variant==0?1.:3.;
        auto lesson=model();lesson.id="qa_manual_forward_latest_feedback";lesson.pages.resize(1);lesson.compiler_puzzles_required=true;
        auto& blocks=lesson.pages.front().blocks;blocks.clear();
        for(const auto index:{25u,38u,39u,41u}){auto block=source.at(index);block.id=block.stable_id="reported_"+std::to_string(index);
            block.address={0,blocks.size(),{},{}};blocks.push_back(std::move(block));}
        const auto root=std::filesystem::path(L"C:/LGAED-QA")/(L"manual-forward-latest-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        Fixture f(backend,false,true,true,"es",lesson,true,0,true,true,[&](HWND view){
            update_lesson_preferences(view,[&](state::StateData& data){data.preferences["lesson_narration_settings"]["rate"]=rate;
                data.preferences["lesson_narration_settings"]["kokoro_voice:es"]=std::string("em_santa");});
        },{},root/L"audio");f.reader->set_qa_playback_muted(true);f.reader->pause();
        const auto choice=child(f.view,1020+static_cast<int>(*blocks[0].correct_index)*2);
        SendMessageW(f.view,WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(choice),BN_CLICKED),reinterpret_cast<LPARAM>(choice));
        SendMessageW(child(f.view,1003),BM_CLICK,0,0);
        wait([&]{const auto value=f.snapshot();return value.planned&&value.segments[value.index].block==0&&value.segments[value.index].feedback&&value.segments[value.index].correct;},
            "first actual correct answer selects its original long feedback",120000);
        if(!f.snapshot().playing)f.reader->toggle();
        wait([&]{const auto value=f.snapshot();const auto& part=value.segments[value.index];
            return value.playing&&part.block==0&&part.feedback&&part.duration_ms>7200&&value.offset>=1400&&value.offset<=1900;
        },"real original feedback supplies a partial heard prefix before the five-second seek",90000);
        f.reader->pause();const auto heard=f.snapshot();f.reader->seek_seconds(5);f.reader->pause();const auto skipped=f.snapshot();
        check(skipped.index==heard.index&&skipped.offset==heard.offset+5000&&!skipped.segments[skipped.index].feedback_fully_played,
            "The manual-forward fixture did not create its real same-segment unheard gap");
        const auto history=narration::read_history(read_lesson_state(f.view));const auto bookmark=std::ranges::find(history.entries,skipped.key,&narration::Bookmark::key);
        check(bookmark!=history.entries.end()&&bookmark->position.heard_prefix&&
            bookmark->position.heard_prefix->microseconds+4000000<bookmark->position.milliseconds*1000,
            "Seeking forward falsely certified the skipped real PCM as heard");
        std::cout<<"MANUAL_FORWARD_GAP reopen="<<reopen<<" legacy="<<legacy<<" rate="<<rate<<" index="<<skipped.index<<" offset="<<skipped.offset
            <<" heard_prefix_us="<<bookmark->position.heard_prefix->microseconds<<" source="<<skipped.segments[skipped.index].source<<'\n';
        const auto first_feedback=std::ranges::find_if(skipped.segments,[](const auto& part){return part.block==0&&part.feedback&&part.correct;});
        check(first_feedback!=skipped.segments.end()&&static_cast<std::size_t>(first_feedback-skipped.segments.begin())==skipped.index,
            "The controlled manual gap is not in the first original feedback sentence");
        if(legacy){
            // Emulate only the absent optional field of the older format. The
            // actual PCM, real partial certificate, source plan and +5s offset
            // remain unchanged in this private QA profile.
            f.reader.reset();
            update_lesson_preferences(f.view,[&](state::StateData& data){auto saved=narration::read_history(data);
                const auto target=std::ranges::find(saved.entries,skipped.key,&narration::Bookmark::key);
                check(target!=saved.entries.end()&&target->position.heard_prefix==bookmark->position.heard_prefix&&
                    target->position.milliseconds==skipped.offset,"Legacy QA lost its actual partial recording evidence");
                auto expected=saved;std::ranges::find(expected.entries,skipped.key,&narration::Bookmark::key)->position.feedback_admissions.clear();
                target->position.feedback_admissions.clear();narration::write_history(data,saved);
                check(narration::read_history(data)==expected,"Legacy QA altered more than the target's optional admission list");
            });
            f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});f.reader->set_qa_playback_muted(true);f.ready();
            const auto restored=f.snapshot();check(!restored.playing&&!restored.allocated&&restored.key==skipped.key&&restored.index==skipped.index&&
                restored.offset==skipped.offset&&restored.heard_through==skipped.heard_through&&!restored.segments[restored.index].feedback_fully_played,
                "Legacy gap restoration changed its genuine pause or invented hearing credit");
        }
        f.reader->toggle();std::optional<std::size_t> remainder_index;
        wait([&]{const auto value=f.snapshot();const auto& part=value.segments[value.index];
            if(!remainder_index||*remainder_index!=value.index){remainder_index=value.index;
                std::cout<<"MANUAL_FORWARD_REMAINDER reopen="<<reopen<<" legacy="<<legacy<<" rate="<<rate<<" index="<<value.index<<" block="<<part.block
                    <<" offset="<<value.offset<<" playing="<<value.playing<<" waiting="<<value.waiting_gate<<" source="<<part.source<<'\n';}
            if(!value.waiting_gate||value.playing||part.block!=1)return false;
            std::size_t played_rest=0;for(std::size_t index=0;index<value.segments.size();++index){const auto& feedback=value.segments[index];
                if(feedback.block!=0||!feedback.feedback||!feedback.correct)continue;
                if(index==skipped.index)check(!feedback.feedback_fully_played,"The actual manual gap falsely became contiguous hearing credit");
                else{check(feedback.feedback_fully_played,"The newer answer was attempted before another original feedback sentence really played");++played_rest;}}
            check(played_rest>0&&value.heard_through<=static_cast<std::int64_t>(skipped.index),
                "The real discontiguous feedback fixture has no remaining played sentences or fabricated a global heard prefix");return true;
        },"after the manual seek, every remaining original feedback sentence really plays before the next unanswered gate",90000);
        f.reader->pause();const auto before_latest=f.snapshot();
        if(reopen){f.reader.reset();check(flush_lesson_view(f.view),"Cannot flush the isolated manual-forward pause");
            f.reader=std::make_unique<LessonReader>(f.view,f.environment,backend,[]{});f.reader->set_qa_playback_muted(true);f.ready();
            const auto restored=f.snapshot();check(!restored.playing&&!restored.allocated&&restored.key==before_latest.key&&restored.index==before_latest.index&&restored.offset==before_latest.offset,
                "Reopening forgot the genuine next-exercise pause after manual preview and complete remaining feedback");}
        const auto edit=child(f.view,1102);const auto answer=wide(blocks[1].answers.front());CHARRANGE all{0,-1};
        SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&all));SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(answer.c_str()));
        SendMessageW(child(f.view,1103),BM_CLICK,0,0);
        wait([&]{const auto value=f.snapshot();return value.planned&&value.segments[value.index].block==1&&value.segments[value.index].feedback&&value.segments[value.index].correct;},
            "latest actual coupon answer selects its own complete feedback",120000);
        f.reader->toggle();bool latest_played=false,title_played=false;
        wait([&]{const auto value=f.snapshot();const auto& part=value.segments[value.index];
            check(!value.playing||part.block!=0,"Completing the latest answer replayed older explicitly skipped feedback");
            if(value.playing&&part.block==1&&part.feedback){check(value.playback_rate==rate,"Latest feedback changed the requested actual Windows playback rate");latest_played=true;}
            if(value.playing&&part.block==2){check(latest_played&&part.source=="Reparar un contador de tareas","The latest coupon feedback skipped its repair title");
                for(const auto& feedback:value.segments)if(feedback.block==1&&feedback.feedback&&feedback.correct)
                    check(feedback.feedback_fully_played,"The repair title bypassed genuinely unheard latest-answer PCM");title_played=true;}
            if(!value.waiting_gate||value.playing||part.block!=3)return false;
            check(title_played&&!value.segments[skipped.index].feedback_fully_played,
                "Manual preview either blocked its later title or forged hearing credit for the old gap");return true;
        },"latest complete feedback follows explicit manual preview and reaches its title/required compiler",90000);
        for(std::size_t index=0;index<2;++index){const auto saved=state::get_response(read_lesson_state(f.view),lesson,blocks[index]);
            check(saved&&saved->correct,"Manual-forward continuation lost an actual checked answer");}
        check(state::read_bytes(published_asset)==original,"Manual-forward QA changed its real source lesson");
        std::cout<<"PASS manual-forward +5s: actual partial PCM prefix remains honest, other original sentences really played, newer answer reads completely then its title, unresolved compiler still gates; reopen="<<reopen<<" legacy="<<legacy<<" rate="<<rate<<" root="<<root.string()<<'\n';
    }
}
}
