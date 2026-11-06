#pragma once

// Included in lesson_tests.cpp's anonymous namespace. State payloads below
// are synthetic unit data; they do not represent curricular work or hearing.
void u12_composed_output_contract(const std::filesystem::path& before_path,
        const std::filesystem::path& after_path) {
    using namespace lgaed;
    using namespace winrt::Windows::Data::Json;
    const auto before_bytes=state::read_bytes(before_path),after_bytes=state::read_bytes(after_path);
    check(state::sha256(before_bytes)=="be285a02171b33a68b30900a7739ffd8b8c5ce5ccee98b6f78ec73f41ad797be"
        &&state::sha256(after_bytes)=="eb160976072ae3bb891ea5fab0bb1336a7d24bb0a83282756634d5189814a6f3",
        "The U12L4 fixtures differ from the complete pinned unmapped/v2 publications");
    const auto before=windows::load_lesson(before_path),after=windows::load_lesson(after_path);
    const auto prior=windows::parse_lesson_progress_metadata(before.raw_json),metadata=windows::parse_lesson_progress_metadata(after.raw_json);
    const auto old_root=JsonObject::Parse(winrt::to_hstring(before.raw_json)),root=JsonObject::Parse(winrt::to_hstring(after.raw_json));
    const auto fingerprint=[](const auto& value){return test::compile_check_test_fingerprint(value);};
    const auto block=[](const JsonObject& source,unsigned p,unsigned b){return source.GetNamedArray(L"pages").GetObjectAt(p).GetNamedArray(L"blocks").GetObjectAt(b);};
    const auto layout=root.GetNamedObject(L"stateLayout");
    check(before.id=="cpp_fundamentals__archivos_y_programa_completo__componer_salida_sin_imprimirla"&&before.id==after.id
        &&before.locale=="es"&&after.locale=="es"&&old_root.Size()==3&&!old_root.HasKey(L"stateLayout")
        &&before.state_layout.version==0&&prior.state_layout.version==0&&after.state_layout.version==2
        &&metadata.state_layout.version==2&&root.Size()==4&&before.pages.size()==5&&after.pages.size()==5
        &&before.state_layout.reading_revisions.empty()&&prior.state_layout.reading_revisions.empty(),
        "The authentic U12L4 pair lost its unmapped predecessor, first v2 adoption or language/owner");
    check(layout.GetNamedArray(L"additions").Size()==0&&layout.GetNamedArray(L"editorial").Size()==8
        &&fingerprint(layout.GetNamedObject(L"family"))=="f9865100c59e76353575893fd342d00b0ce5eee3ef2c9605a4b9189db4fbd887"
        &&fingerprint(layout.GetNamedArray(L"origin"))=="6c5a4300e032dfde02180cfb468cbb2b418931fbc65fb8f771561aa30b2dbee3"
        &&fingerprint(layout.GetNamedArray(L"current"))=="430aa6b167fac29d684c9c45734b6c611ce06be9bbd16a303c598d1c562a6e92"
        &&fingerprint(layout.GetNamedArray(L"editorial"))=="1ad25edf3618c1f51a1e21504b52bbc65f27d70b7deba6ef5344d126912c345b"
        &&fingerprint(old_root.GetNamedArray(L"pages"))=="ffeb1349bb0fcd7d1bc9e7002c344ce68e3fcf4a8162464e9b908606521010a8"
        &&fingerprint(root.GetNamedArray(L"pages"))=="b3ee4c125cf8fbc0242a4a0cbb24f34dc03677d84ad6485c7c319c8fbd878852",
        "The U12L4 adoption lost its full five-page payload, family, maps or exact ordered eight proofs");
    struct Change{const char* id;unsigned page,index;bool prose;const char* previous;const char* current;};
    const std::array<Change,8> changes{{
        {"block_0000_0001",0,1,true,"c02adc2d24a66e7b0dce4f5aa5692cb2f204850bb1552792ca1bd5e2087c6b19","b3d34bd31f3e0109c48641f6c3bd16dd75eddaeda0a9788611bd595e79d73f21"},
        {"block_0000_0003",0,3,true,"53da4b1c7e1185a16256c4981ac6e5efc6ae5367e6786724929ebe9ef483b2fc","d90d01bec3b7c8fedd2054b4ce07e9b3694b28f3617a894ce5ab6e88935eac82"},
        {"block_0000_0004",0,4,false,"7a041d7e9cfc4c7f5e8cc7000144fa9123dac81f9af54e4505a19640e2081e58","2834c29f0d3680bdf1fda9e6b573ba3e624ebd507e67d7083e6654d1ef4140fd"},
        {"block_0001_0001",1,1,false,"042cc93bd22860e2760dabc3b06baab868eae9c22a1e89c0d738f16b9f9b264b","79de873c83c4090d903b2ac91b67fe186b053b45ba2566b33ed51bb5e909f114"},
        {"block_0001_0002",1,2,true,"fa2bd564d660cb74e06163a7cbb9986c6fdfb9bb83a6743027e363f38b32b566","a3c73edce6e41ffa8f86f478a5667a53e2ed1801fb43430e89721228e35d5e9b"},
        {"block_0001_0003",1,3,false,"faf6393e200b49d4b9a31be600ebabd33fe4f7b224ff6bfbc6479a7027382480","4d21f3591bfa76c81c6fa7fdc48ffb455745414abdad0fc9cf1cb768b9d7bf0f"},
        {"block_0001_0004",1,4,false,"5259a7687faffd5b6daf28dd2db9a4f73c387bc13c9f78b97affee8c1987b622","7fed11b6ada96a990c364b94264c5152690187a2b59106156cfe225daa2c31b3"},
        {"block_0001_0005",1,5,false,"2b80ca083b2d0eb1b2a5b9492a15ae5ae2095f880cbcb65f36276a6069705109","8216c1ca7b4af782b539a6e72edf5b36ccd0d3fcf98103a21da4453398dbaf22"}
    }};
    const auto restored=JsonObject::Parse(root.Stringify());restored.Remove(L"stateLayout");
    for(unsigned i=0;i<changes.size();++i){const auto& item=changes[i];const auto old=block(old_root,item.page,item.index),now=block(root,item.page,item.index);
        const auto proof=layout.GetNamedArray(L"editorial").GetObjectAt(i);
        check(fingerprint(old)==item.previous&&fingerprint(now)==item.current&&proof.Size()==3
            &&winrt::to_string(proof.GetNamedString(L"id"))==item.id
            &&winrt::to_string(proof.GetNamedString(L"beforeSha256"))==item.previous
            &&winrt::to_string(proof.GetNamedString(L"afterSha256"))==item.current,
            "One of U12L4's three exact prose passages/five guides or ordered proofs changed");
        const auto old_fields=JsonObject::Parse(old.Stringify()),new_fields=JsonObject::Parse(now.Stringify());
        for(const auto field:item.prose?std::vector<std::wstring_view>{L"text",L"emphasis"}:std::vector<std::wstring_view>{L"reviewFeedback"}){
            if(old_fields.HasKey(winrt::hstring(field)))old_fields.Remove(winrt::hstring(field));
            if(new_fields.HasKey(winrt::hstring(field)))new_fields.Remove(winrt::hstring(field));
        }
        check(fingerprint(old_fields)==fingerprint(new_fields),"The U12L4 first adoption changed an exercise contract beyond its new guide");
        restored.GetNamedArray(L"pages").GetObjectAt(item.page).GetNamedArray(L"blocks").SetAt(item.index,old);
    }
    check(fingerprint(restored)==fingerprint(old_root),"The U12L4 adoption changed fields outside its eight authentic editorial blocks");
    const std::array<std::size_t,5> shape{5,6,5,6,3};std::size_t identities=0,sources=0,activities=0;
    for(std::size_t p=0;p<shape.size();++p){
        check(before.pages[p].blocks.size()==shape[p]&&after.pages[p].blocks.size()==shape[p]
            &&storage_page(before,p)==storage_page(after,p),"The U12L4 adoption moved a historical page address");
        for(std::size_t b=0;b<shape[p];++b){const auto& old=before.pages[p].blocks[b];const auto& now=after.pages[p].blocks[b];++identities;
            sources+=!old.code.empty();sources+=!old.initial_code.empty();
            activities+=old.type==BlockType::multiple_choice||old.type==BlockType::output_prediction;
            check(old.id==now.id&&old.type==now.type&&old.question==now.question&&old.options==now.options&&old.correct_index==now.correct_index
                &&old.answers==now.answers&&old.explanation==now.explanation&&old.code==now.code&&old.initial_code==now.initial_code
                &&exercise_key(before,old)==exercise_key(after,now)&&draft_key("programming",before,old)==draft_key("programming",after,now)
                &&exercise_key(after,now)==exercise_key(metadata,metadata.pages[p].blocks[b]),
                "The U12L4 first adoption changed IDs, answers, code or response/draft storage ownership");
        }
    }
    check(identities==25&&sources==7&&activities==6,"The real U12L4 fixture lost identities, seven code fields or six independent exercises");
    for(const auto* model:{&after,&metadata}){const auto& revisions=model->state_layout.reading_revisions;
        check(revisions.size()==1&&revisions[0].editorial_prefix==8&&revisions[0].positional_page_identity&&revisions[0].previous_blocks.size()==8,
            "The U12L4 first adoption lacks its eight-block positional old-reading provenance");
        const auto historical=JsonObject::Parse(root.Stringify());historical.Remove(L"stateLayout");
        for(const auto& item:changes){const auto found=revisions[0].previous_blocks.find(item.id);
            check(found!=revisions[0].previous_blocks.end()&&fingerprint(JsonObject::Parse(winrt::to_hstring(found->second)))==item.previous,
                "The U12L4 archive lost the complete authentic old paragraph or unreviewed exercise");
            historical.GetNamedArray(L"pages").GetObjectAt(item.page).GetNamedArray(L"blocks").SetAt(item.index,JsonObject::Parse(winrt::to_hstring(found->second)));
        }
        check(fingerprint(historical)==fingerprint(old_root),"The U12L4 prior reading snapshot fabricated or omitted an independent old page/block");
    }
    check(after.state_layout.reading_revisions[0].previous_blocks==metadata.state_layout.reading_revisions[0].previous_blocks,
        "Runtime/catalog disagree on U12L4's authenticated old script");
    // First adoption may add provenance metadata, but every earlier payload
    // and its positional storage key must remain byte-for-byte intact.
    for(const auto mode:{0,1,2}){
        state::StateData data;state::prepare_lesson_state(data,before);
        data.preferences["u12_composed_contract_qa"]["opaque"]=std::string(" \t conservar\r\n保存");
        for(std::size_t p=0;p<shape.size();++p)for(std::size_t b=0;b<shape[p];++b){const auto& old=before.pages[p].blocks[b];
            if(old.type!=BlockType::multiple_choice&&old.type!=BlockType::output_prediction)continue;
            state::Response response;response.correct=mode==0;response.raw_json=R"({"incorrectAttempts":4,"opaque":{"selection":17}})";
            if(old.type==BlockType::multiple_choice)response.selected=mode==0?static_cast<std::int64_t>(*old.correct_index):mode==1?static_cast<std::int64_t>((*old.correct_index+1)%old.options.size()):-1;
            else response.answer=mode==0?old.answers.front():mode==1?"QA incorrecto":"QA incompleto";
            if(mode==1)state::mark_response_reviewed(response,old);state::put_response(data,before,old,response);
        }
        const auto& compiler=before.pages.at(2).blocks.at(3);check(compiler.type==BlockType::cpp_compiler,"The independent U12L4 draft owner changed");
        const auto key=draft_key("programming",before,compiler);
        state::Draft draft;draft.source=compiler.initial_code+"// QA: conservar borrador\n";draft.input=" entrada\t\r\n";
        draft.indentation_json=state::make_indentation_metadata(draft.source,{});draft.selection_start=3;draft.selection_end=11;
        state::put_draft(data,key,draft);state::save_current_page(data,before,4);const auto saved=data;
        state::prepare_lesson_state(data,after);state::validate_lesson_state(data,after);
        for(const auto& [domain,entries]:saved.preferences)for(const auto& [name,value]:entries){const auto retained=state::find(data,domain,name);
            check(retained&&*retained==value,"The U12L4 adoption overwrote an existing state payload or opaque preference");}
        for(std::size_t p=0;p<shape.size();++p)for(std::size_t b=0;b<shape[p];++b){const auto& old=before.pages[p].blocks[b];
            if(old.type!=BlockType::multiple_choice&&old.type!=BlockType::output_prediction)continue;
            const auto previous=state::get_response(saved,before,old),current=state::get_response(data,after,after.pages[p].blocks[b]);
            check(previous&&current&&previous->raw_json==current->raw_json&&previous->answer==current->answer&&previous->selected==current->selected
                &&previous->correct==current->correct&&state::response_reviewed(*current,after.pages[p].blocks[b])==(mode==1),
                "The U12L4 v0-to-v2 adoption changed a correct, reviewed error or unfinished answer");
        }
        const auto retained=state::get_draft(data,key,after.pages[2].blocks[3].initial_code);
        check(retained.has_saved&&retained.source==draft.source&&retained.input==draft.input&&retained.indentation_json==draft.indentation_json
            &&retained.selection_start==draft.selection_start&&retained.selection_end==draft.selection_end&&state::current_page(data,after)==4,
            "The U12L4 adoption changed independent source/input/indentation/selection or page position");
    }
    const auto no_archive=[](const JsonObject& source,std::string_view locale="es"){
        const auto raw=winrt::to_string(source.Stringify());
        check(windows::parse_lesson_json(raw,locale).state_layout.reading_revisions.empty(),"An unreviewed U12L4 variant borrowed positional reading provenance");
        check(windows::parse_lesson_progress_metadata(raw,locale).state_layout.reading_revisions.empty(),"Catalog borrowed U12L4 historical reading for another valid publication");
    };
    const auto refresh=[&](const JsonObject& source,unsigned index){const auto& item=changes[index];
        source.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").GetObjectAt(index).Insert(L"afterSha256",
            JsonValue::CreateStringValue(winrt::to_hstring(fingerprint(block(source,item.page,item.index)))));};
    for(const auto mode:{"owner","locale","family","origin","current","editorial","before_proof","no_proof","paragraph","guide","neighbor"}){
        const auto altered=JsonObject::Parse(root.Stringify()),map=altered.GetNamedObject(L"stateLayout");const auto change=std::string_view(mode);
        if(change=="owner")altered.Insert(L"id",JsonValue::CreateStringValue(L"another_composed_output_owner"));
        else if(change=="locale"){map.Insert(L"locale",JsonValue::CreateStringValue(L"en"));map.GetNamedObject(L"family").Insert(L"en",JsonObject::Parse(map.GetNamedObject(L"family").GetNamedObject(L"es").Stringify()));}
        else if(change=="family")map.GetNamedObject(L"family").GetNamedObject(L"fr").Insert(L"sha256",JsonValue::CreateStringValue(winrt::to_hstring(std::string(64,'0'))));
        else if(change=="origin"){
            const auto blocks=map.GetNamedArray(L"origin").GetObjectAt(0).GetNamedArray(L"blocks");const auto first=blocks.GetAt(0);blocks.SetAt(0,blocks.GetAt(2));blocks.SetAt(2,first);
            map.GetNamedObject(L"family").GetNamedObject(L"es").Insert(L"origin",JsonArray::Parse(map.GetNamedArray(L"origin").Stringify()));
        }else if(change=="current"){
            const auto blocks=altered.GetNamedArray(L"pages").GetObjectAt(0).GetNamedArray(L"blocks"),ids=map.GetNamedArray(L"current").GetObjectAt(0).GetNamedArray(L"blocks");
            const auto first=blocks.GetAt(0),id=ids.GetAt(0);blocks.SetAt(0,blocks.GetAt(2));blocks.SetAt(2,first);ids.SetAt(0,ids.GetAt(2));ids.SetAt(2,id);
        }else if(change=="editorial"){
            const auto edges=map.GetNamedArray(L"editorial");const auto first=edges.GetAt(0);edges.SetAt(0,edges.GetAt(1));edges.SetAt(1,first);
        }else if(change=="before_proof")map.GetNamedArray(L"editorial").GetObjectAt(0).Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(std::string(64,'0'))));
        else if(change=="no_proof")map.GetNamedArray(L"editorial").RemoveAtEnd();
        else if(change=="paragraph"){block(altered,0,1).Insert(L"text",JsonValue::CreateStringValue(L"Otra precisión editorial coherente."));refresh(altered,0);}
        else if(change=="guide"){block(altered,0,4).GetNamedArray(L"reviewFeedback").SetAt(0,JsonValue::CreateStringValue(L"Otra guía editorial coherente."));refresh(altered,2);}
        else {const auto item=block(altered,2,1);const auto old=fingerprint(item);item.Insert(L"text",JsonValue::CreateStringValue(L"Otra prosa posterior fuera de las ocho revisiones."));
            JsonObject edge;edge.Insert(L"id",JsonValue::CreateStringValue(L"block_0002_0001"));edge.Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(old)));
            edge.Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(fingerprint(item))));map.GetNamedArray(L"editorial").Append(edge);}
        no_archive(altered,change=="locale"?"en":"es");
    }
    for(const auto mode:{"identity","type","answer","source","correct","option","after_proof","origin_signature","unknown_current","injected","positional_injected"}){
        const auto altered=JsonObject::Parse(root.Stringify()),map=altered.GetNamedObject(L"stateLayout");const auto change=std::string_view(mode);
        if(change=="identity")block(altered,1,1).Insert(L"id",JsonValue::CreateStringValue(L"another_prediction_owner"));
        else if(change=="type")block(altered,1,1).Insert(L"type",JsonValue::CreateStringValue(L"fill_code"));
        else if(change=="answer")block(altered,1,1).GetNamedArray(L"answers").SetAt(0,JsonValue::CreateStringValue(L"Otro resultado"));
        else if(change=="source")block(altered,1,1).Insert(L"code",JsonValue::CreateStringValue(L"int main(){}\n"));
        else if(change=="correct")block(altered,0,4).Insert(L"correctIndex",JsonValue::CreateNumberValue(0));
        else if(change=="option")block(altered,0,4).GetNamedArray(L"options").SetAt(0,JsonValue::CreateStringValue(L"Otra opción no autorizada"));
        else if(change=="after_proof")map.GetNamedArray(L"editorial").GetObjectAt(0).Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(std::string(64,'0'))));
        else if(change=="origin_signature")map.GetNamedArray(L"origin").GetObjectAt(0).GetNamedArray(L"blocks").GetObjectAt(1).Insert(L"sha256",JsonValue::CreateStringValue(winrt::to_hstring(std::string(64,'0'))));
        else if(change=="unknown_current")map.GetNamedArray(L"current").GetObjectAt(0).GetNamedArray(L"blocks").SetAt(0,JsonValue::CreateStringValue(L"unknown_composed_identity"));
        else if(change=="injected")map.Insert(L"reading_revisions",JsonArray{});
        else map.Insert(L"positional_page_identity",JsonValue::CreateBooleanValue(true));
        if(change=="identity"||change=="type"||change=="answer"||change=="source")refresh(altered,3);
        if(change=="correct"||change=="option")refresh(altered,2);
        const auto raw=winrt::to_string(altered.Stringify());
        rejects([&]{(void)windows::parse_lesson_json(raw);},"An incompatible/injected U12L4 contract passed the runtime loader");
        rejects([&]{(void)windows::parse_lesson_progress_metadata(raw);},"An incompatible/injected U12L4 contract passed catalog metadata");
    }
    check(state::read_bytes(before_path)==before_bytes&&state::read_bytes(after_path)==after_bytes,"U12L4 contract verification rewrote its immutable fixture inputs");
    std::cout<<"PASS U12L4 first-v2 contract: exact unmapped/current five-page pair, 25 identities/seven sources/eight editorial changes, eight complete positional old blocks in runtime/catalog, response and independent draft preservation across correct/error/unfinished unit states, coherent variants without borrowed history and incompatible payload rejection; no UI, execution or listening claim\n";
}
