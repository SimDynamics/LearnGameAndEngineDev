#pragma once
// Included after the existing hidden View and source/shading helpers. These
// tests mount complete current lessons, never copies with invented answers.
void current_console_string_references(){
    const auto neutral=[](HWND target,std::wstring_view context,std::wstring_view token){
        const auto start=passage_offset(target,context,token);
        for(std::size_t at=0;at<token.size();++at){
            if(std::wstring_view(L" \t\r\n").find(token[at])!=std::wstring_view::npos)continue;
            check(character(target,static_cast<LONG>(start+at)).crTextColor==reader_theme::foreground(target),
                "A displayed console phrase inherited identifier/string ink instead of the current neutral foreground");
            reference_background(target,start+at);
        }
    };
    const auto choose=[](View& f,std::size_t block,std::size_t option){
        SendMessageW(f.item(block,20+static_cast<int>(option)*2),BM_CLICK,0,0);pump();
        check(edit_text(f.item(block,4)).empty(),"Selecting an option revealed feedback before Check");
        SendMessageW(f.item(block,3),BM_CLICK,0,0);pump();
    };
    struct ChoiceCase{const char* file;std::size_t page,block;bool strings;};
    for(const auto test:{ChoiceCase{"programas_y_ejecucion/lesson_primer_programa_y_salida.json",3,3,false},
            ChoiceCase{"objetos_y_estado/lesson_cadenas_y_texto.json",4,3,true}}){
        const auto lesson=load(test.file);const auto& goal=lesson.pages.at(test.page).blocks.at(test.block);
        const auto bytes=state::read_bytes(lesson.source_path);View f(lesson,test.page,true);
        check(goal.type==curriculum::BlockType::multiple_choice&&goal.options.size()==3&&goal.correct_index.has_value(),
            "The actual console/string choice owner changed");
        const auto verify=[&]{
            const auto question=f.item(test.block,0);
            check(edit_text(question)==visible_prose(goal.question),"Contextual formatting changed the current question");
            if(test.strings){
                role_reference(f,question,L"\"04\"",L"\"04\"");
                role_reference(f,question,L"\"2\"",L"\"2\"");
            }else{
                role_reference(f,question,L"std::cout",L"std::cout");
                role_reference(f,question,L"\"Listo\"",L"\"Listo\"");
            }
            source_and_shading(f,question,goal.question,goal.emphasis);
            for(std::size_t option=0;option<goal.options.size();++option){
                const auto target=f.item(test.block,21+static_cast<int>(option)*2);
                const auto text=wide(edit_text(target));
                check(edit_text(target)==visible_prose(goal.options[option]),"Contextual formatting rewrote an actual option");
                if(!test.strings)neutral(target,text,text);
                else if(option!=1)role_reference(f,target,text,text);
                else neutral(target,text,L"42");
                source_and_shading(f,target,goal.options[option]);
            }
        };
        verify();check(edit_text(f.item(test.block,4)).empty(),"An untouched choice exposed its explanation");
        for(std::size_t option=0;option<goal.options.size();++option)if(option!=goal.correct_index){
            choose(f,test.block,option);verify();
            const auto rejected=state::get_response(read_lesson_state(f.view),lesson,goal);
            check(rejected&&!rejected->correct&&rejected->selected==static_cast<std::int64_t>(option),
                "Formatting granted success to a wrong console/string choice");
        }
        choose(f,test.block,*goal.correct_index);verify();
        const auto before=state::get_response(read_lesson_state(f.view),lesson,goal);
        check(before&&before->correct,"The actual console/string choice was not answered through its control");
        f.reopen();verify();const auto after=state::get_response(read_lesson_state(f.view),lesson,goal);
        check(after&&after->raw_json==before->raw_json&&after->correct&&after->selected==before->selected
            &&state::read_bytes(lesson.source_path)==bytes,"Coloring/reopening altered the actual answer or asset");
    }
    {
        const auto lesson=load("programas_y_ejecucion/lesson_texto_secuencia_y_formato.json");
        const auto bytes=state::read_bytes(lesson.source_path);constexpr std::size_t page=4,block=1;
        const auto& goal=lesson.pages.at(page).blocks.at(block);View f(lesson,page,true);
        check(goal.id=="literal_y_operador"&&goal.type==curriculum::BlockType::inline_fill_code,
            "The actual mixed literal/output explanation owner changed");
        check(edit_text(f.item(block,4)).empty(),"A mixed explanation appeared before the actual answer");
        const auto holes=f.item(block,5);
        for(const auto& [id,text]:std::array<std::pair<int,const wchar_t*>,2>{{{100,L"<<"},{101,L"\"Carga lista\""}}}){
            const auto edit=GetDlgItem(holes,id);check(edit,"A mixed-reference QA hole is missing");
            SendMessageW(edit,EM_SETSEL,0,-1);SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(text));
        }
        SendMessageW(f.item(block,3),BM_CLICK,0,0);pump();
        const auto verify=[&]{
            const auto target=f.item(block,4);const auto raw="✓ Correcto\n\n"+goal.explanation;
            check(edit_text(target)==visible_prose(raw),"The mixed literal/output explanation was truncated or rewritten");
            role_reference(f,target,L"el literal \"Carga lista\"",L"\"Carga lista\"");
            neutral(target,L"La salida es Carga lista",L"Carga lista");
            role_reference(f,target,L"std::cout",L"std::cout");
            source_and_shading(f,target,raw);
            const auto response=state::get_response(read_lesson_state(f.view),lesson,goal);
            check(response&&response->correct,"The mixed-reference answer lost its actual success");
        };
        verify();const auto before=state::get_response(read_lesson_state(f.view),lesson,goal);
        f.reopen();verify();const auto after=state::get_response(read_lesson_state(f.view),lesson,goal);
        check(after&&before&&after->raw_json==before->raw_json&&state::read_bytes(lesson.source_path)==bytes,
            "Formatting/reopening changed the mixed-reference answer or lesson source");
    }
    {
        const auto lesson=load("archivos_y_programa_completo/lesson_componer_salida_sin_imprimirla.json");
        const auto bytes=state::read_bytes(lesson.source_path);
        const auto paragraph=curriculum::stable_block_position(lesson,"block_0000_0003");
        const auto choice=curriculum::stable_block_position(lesson,"block_0001_0005");
        check(paragraph&&choice&&paragraph->page==0&&paragraph->block==3&&choice->page==1&&choice->block==5,
            "The actual U12L4 composed-result owners changed");
        const auto& prose=lesson.pages[paragraph->page].blocks[paragraph->block];
        const auto& goal=lesson.pages[choice->page].blocks[choice->block];
        check(prose.type==curriculum::BlockType::paragraph&&goal.type==curriculum::BlockType::multiple_choice
            &&goal.options==std::vector<std::string>{"Validos=2;Rechazados=1Validos=0;Rechazados=0","Validos=0;Rechazados=0",
                "Una cadena vacía, porque todos los valores son cero."},"The current U12L4 data/prose alternatives changed");
        // These two pages precede the required compiler. No prerequisite
        // receipt, puzzle answer or listening position is fabricated here.
        View first(lesson,paragraph->page,false,{},false);
        const auto verify_prose=[&]{
            const auto target=first.item(paragraph->block,0);
            check(edit_text(target)==visible_prose(prose.text),"The current composed-result paragraph changed its complete source");
            neutral(target,L"forman el texto Cajas=4;Estado=listo dentro",L"Cajas=4;Estado=listo");
            neutral(target,L"se ve Cajas=4;Estado=listo, sin",L"Cajas=4;Estado=listo");
            role_reference(first,target,L"texto << \"Cajas=\"",L"\"Cajas=\"");
            role_reference(first,target,L"inserción del entero 4",L"4");
            role_reference(first,target,L"aporta el dígito 4",L"4",true);
            ordinary_span(target,L"Las cadenas aportan las etiquetas",L"Las cadenas aportan las etiquetas");
            source_and_shading(first,target,prose.text,prose.emphasis);
        };
        for(const auto accent:{RGB(238,51,77),RGB(82,223,241)}){
            reader_theme::set_accent(first.host,accent);
            for(const int zoom:{75,100,150,200}){
                SendMessageW(first.item(paragraph->block,0),EM_SETZOOM,zoom,100);verify_prose();
            }
        }
        SendMessageW(first.item(paragraph->block,0),EM_SETZOOM,0,0);first.reopen();verify_prose();
        View second(lesson,choice->page,false,{},false);
        const auto verify_choice=[&]{
            const auto question=second.item(choice->block,0);
            check(edit_text(question)==visible_prose(goal.question),"A result role changed the actual question");
            source_and_shading(second,question,goal.question,goal.emphasis);
            for(std::size_t option=0;option<goal.options.size();++option){
                const auto target=second.item(choice->block,21+static_cast<int>(option)*2);
                const auto sample=wide(goal.options[option]);
                check(edit_text(target)==goal.options[option],"A composed-chain alternative was rewritten");
                if(option<2)neutral(target,sample,sample);else ordinary_span(target,sample,sample);
                source_and_shading(second,target,goal.options[option]);
            }
        };
        check(edit_text(second.item(choice->block,4)).empty(),"An untouched composed-data choice exposed feedback");
        for(const int zoom:{75,100,150,200}){
            for(std::size_t option=0;option<goal.options.size();++option)
                SendMessageW(second.item(choice->block,21+static_cast<int>(option)*2),EM_SETZOOM,zoom,100);
            verify_choice();
        }
        for(std::size_t option=0;option<goal.options.size();++option)if(option!=goal.correct_index){
            choose(second,choice->block,option);verify_choice();
            const auto response=state::get_response(read_lesson_state(second.view),lesson,goal);
            check(response&&!response->correct&&response->selected==static_cast<std::int64_t>(option),
                "Contextual result formatting granted success to an incorrect alternative");
        }
        choose(second,choice->block,*goal.correct_index);verify_choice();
        const auto before=state::get_response(read_lesson_state(second.view),lesson,goal);
        check(before&&before->correct,"The actual composed-data choice was not accepted through its control");
        second.reopen();verify_choice();const auto after=state::get_response(read_lesson_state(second.view),lesson,goal);
        check(after&&after->raw_json==before->raw_json&&after->correct&&after->selected==before->selected
            &&state::read_bytes(lesson.source_path)==bytes,"A composed-result color/reopen changed the answer or asset");
        std::cout<<"PASS actual U12L4 composed results: both paragraph occurrences and both data alternatives use native neutral ink/backdrop; distinct marked C++ integer/string and ordinary prose preserved; four zooms, two accents, pending gray, exact source/state/reopen, no compiler receipts/execution or hearing claimed\n";
    }
    std::cout<<"PASS actual U1/U2 console and string references: all choices including wrong samples, mixed literal/output feedback, current ink/backdrop, pending gray/restoration, exact source/selection/state and silent reopen; compiler prerequisite receipts are synthetic formatting QA, no execution/listening claim\n";
}
