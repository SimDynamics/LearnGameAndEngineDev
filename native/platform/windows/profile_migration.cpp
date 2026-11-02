#include "platform/windows/profile_migration.hpp"
#include "platform/windows/state_store.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <limits>
#include <stdexcept>

namespace lgaed::windows {
namespace {
struct File {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~File(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
struct Hasher {
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    BCRYPT_HASH_HANDLE hash=nullptr;
    Hasher(){
        if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)
            throw std::runtime_error("No se pudo verificar la copia del progreso.");
        if(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0){
            BCryptCloseAlgorithmProvider(algorithm,0);algorithm=nullptr;
            throw std::runtime_error("No se pudo iniciar la verificación del progreso.");
        }
    }
    ~Hasher(){if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}
    void append(std::span<const std::uint8_t> bytes){
        if(BCryptHashData(hash,const_cast<PUCHAR>(bytes.data()),static_cast<ULONG>(bytes.size()),0)<0)
            throw std::runtime_error("No se pudo comprobar un archivo del progreso.");
    }
    std::string finish(){
        std::array<std::uint8_t,32> bytes{};
        if(BCryptFinishHash(hash,bytes.data(),static_cast<ULONG>(bytes.size()),0)<0)
            throw std::runtime_error("No se pudo completar la verificación del progreso.");
        constexpr char hex[]="0123456789abcdef";std::string result;
        for(const auto value:bytes){result+=hex[value>>4];result+=hex[value&15];}
        return result;
    }
};
struct Entry {
    std::string path;
    bool directory=false;
    std::uint64_t bytes=0;
    std::string hash;
    bool operator==(const Entry&)const=default;
};
using Manifest=std::vector<Entry>;

std::string utf8(const std::filesystem::path& path){
    const auto text=path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()),text.size()};
}
std::filesystem::path relative_path(std::string_view value){
    const auto first=reinterpret_cast<const char8_t*>(value.data());
    const std::filesystem::path path(std::u8string(first,first+value.size()));
    if(value.empty()||value.find('\0')!=value.npos||path.has_root_path()||path==L"."||
        path.lexically_normal()!=path||utf8(path)!=value)
        throw std::runtime_error("El manifiesto de migración contiene una ruta no admitida.");
    for(const auto& part:path)if(part==L"..")
        throw std::runtime_error("El manifiesto de migración sale de su carpeta.");
    return path;
}
bool same_path(const std::filesystem::path& one,const std::filesystem::path& two){
    return CompareStringOrdinal(one.c_str(),-1,two.c_str(),-1,TRUE)==CSTR_EQUAL;
}
bool within(const std::filesystem::path& child,const std::filesystem::path& parent){
    auto at=child.begin();
    for(auto part=parent.begin();part!=parent.end();++part,++at)
        if(at==child.end()||!same_path(*at,*part))return false;
    return true;
}
std::filesystem::path profile_path(const std::filesystem::path& path){
    if(!path.is_absolute())throw std::runtime_error("La carpeta del perfil debe ser absoluta.");
    auto result=path.lexically_normal();
    while(result!=result.root_path()&&result.filename().empty())result=result.parent_path();
    state::require_safe_path(result);
    if(std::filesystem::exists(state::native_io_path(result))&&
        !std::filesystem::is_directory(state::native_io_path(result)))
        throw std::runtime_error("La ubicación del perfil no es una carpeta.");
    return result;
}
void verify_directory(const std::filesystem::path& path){
    state::require_safe_path(path);
    File directory{CreateFileW(state::native_io_path(path).c_str(),FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};
    if(directory.value==INVALID_HANDLE_VALUE)
        throw std::runtime_error("No se pudo comprobar la carpeta física del progreso.");
    state::require_unredirected_handle(directory.value,path);
}
void make_directories(const std::filesystem::path& path){
    state::require_safe_path(path);std::filesystem::create_directories(state::native_io_path(path));
    verify_directory(path);
}
void validate_state_directory(const std::filesystem::path& profile){
    const auto path=profile/L"state";state::require_safe_path(path);
    if(std::filesystem::exists(state::native_io_path(path))){
        if(!std::filesystem::is_directory(state::native_io_path(path)))
            throw std::runtime_error("La carpeta del estado está incompleta; se conserva sin modificarla.");
        verify_directory(path);
    }
}
Entry file_entry(const std::filesystem::path& source,std::string relative,
        const std::filesystem::path* destination=nullptr){
    state::require_safe_path(source);
    File input{CreateFileW(state::native_io_path(source).c_str(),GENERIC_READ,
        FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr)};
    if(input.value==INVALID_HANDLE_VALUE)throw std::runtime_error("No se pudo leer un archivo del progreso anterior.");
    state::require_unredirected_handle(input.value,source);
    File output;
    if(destination){
        state::require_safe_path(*destination);
        output.value=CreateFileW(state::native_io_path(*destination).c_str(),GENERIC_WRITE,0,nullptr,
            CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
        if(output.value==INVALID_HANDLE_VALUE)throw std::runtime_error("No se pudo crear la copia pendiente del progreso.");
        state::require_unredirected_handle(output.value,*destination);
    }
    Hasher hasher;std::array<std::uint8_t,64*1024> buffer{};std::uint64_t total=0;
    for(;;){
        DWORD count=0;
        if(!ReadFile(input.value,buffer.data(),static_cast<DWORD>(buffer.size()),&count,nullptr))
            throw std::runtime_error("No se pudo leer completo el progreso anterior.");
        if(!count)break;
        if(total>std::numeric_limits<std::uint64_t>::max()-count)
            throw std::runtime_error("Un archivo del progreso supera el tamaño admitido.");
        total+=count;hasher.append(std::span(buffer.data(),count));
        for(DWORD offset=0;destination&&offset<count;){
            DWORD written=0;
            if(!WriteFile(output.value,buffer.data()+offset,count-offset,&written,nullptr)||!written)
                throw std::runtime_error("No se pudo escribir completa la copia del progreso.");
            offset+=written;
        }
    }
    if(destination&&!FlushFileBuffers(output.value))
        throw std::runtime_error("No se pudo confirmar la copia del progreso en disco.");
    return {std::move(relative),false,total,hasher.finish()};
}
Manifest scan(const std::filesystem::path& active,bool published=false){
    verify_directory(active);
    Manifest result;
    for(const auto& item:std::filesystem::recursive_directory_iterator(state::native_io_path(active))){
        state::require_safe_path(item.path());
        const auto relative=item.path().lexically_relative(state::native_io_path(active));
        if(same_path(relative,std::filesystem::path(profile_migration_marker_name))){
            if(published)continue;
            throw std::runtime_error("El perfil anterior contiene un nombre reservado para la migración; se conserva sin cambios.");
        }
        auto name=utf8(relative);(void)relative_path(name);
        if(item.is_directory()){verify_directory(item.path());result.push_back({std::move(name),true,0,{}});}
        else if(item.is_regular_file())result.push_back(file_entry(item.path(),std::move(name)));
        else throw std::runtime_error("El progreso contiene un archivo especial que no se puede migrar.");
    }
    std::ranges::sort(result,{},&Entry::path);return result;
}
std::vector<std::uint8_t> marker_bytes(const Manifest& entries){
    state::StateData data;auto& fields=data.preferences["profile_migration"];
    fields["format"]=std::string("LearnGameAndEngineDev.local-to-roaming.v1");
    std::vector<std::string> values;
    for(const auto& entry:entries){
        values.push_back(entry.directory?"directory":"file");values.push_back(entry.path);
        values.push_back(std::to_string(entry.bytes));values.push_back(entry.hash);
    }
    fields["entries"]=std::move(values);auto bytes=state::encode(data);
    const auto hash=state::sha256(bytes);bytes.insert(bytes.end(),hash.begin(),hash.end());return bytes;
}
void write_marker(const std::filesystem::path& path,const Manifest& entries){
    const auto bytes=marker_bytes(entries);
    File output{CreateFileW(state::native_io_path(path).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr)};
    if(output.value==INVALID_HANDLE_VALUE)throw std::runtime_error("No se pudo crear el manifiesto de migración.");
    state::require_unredirected_handle(output.value,path);
    for(std::size_t at=0;at<bytes.size();){
        DWORD written=0;const auto count=static_cast<DWORD>(std::min<std::size_t>(bytes.size()-at,64*1024));
        if(!WriteFile(output.value,bytes.data()+at,count,&written,nullptr)||!written)
            throw std::runtime_error("No se pudo escribir el manifiesto completo de migración.");
        at+=written;
    }
    if(!FlushFileBuffers(output.value))throw std::runtime_error("No se pudo confirmar el manifiesto de migración.");
}
Manifest read_marker(const std::filesystem::path& path){
    auto bytes=state::read_bytes(path);
    if(bytes.size()<64)throw std::runtime_error("El manifiesto de migración está incompleto; se conserva el progreso.");
    const std::string hash(bytes.end()-64,bytes.end());bytes.resize(bytes.size()-64);
    if(state::sha256(bytes)!=hash)throw std::runtime_error("La integridad del manifiesto de migración no coincide; se conserva el progreso.");
    const auto data=state::decode(bytes);
    if(data.preferences.size()!=1||!data.preferences.contains("profile_migration")||
        data.preferences.at("profile_migration").size()!=2||
        state::string_value(data,"profile_migration","format")!="LearnGameAndEngineDev.local-to-roaming.v1")
        throw std::runtime_error("El manifiesto de migración no es compatible.");
    const auto* value=state::find(data,"profile_migration","entries");
    const auto* values=value?std::get_if<std::vector<std::string>>(value):nullptr;
    if(!values||values->size()%4)throw std::runtime_error("El inventario de migración está incompleto.");
    Manifest result;
    for(std::size_t index=0;index<values->size();index+=4){
        const auto& kind=values->at(index);const auto& name=values->at(index+1);
        const auto& count=values->at(index+2);const auto& digest=values->at(index+3);
        const auto relative=relative_path(name);std::uint64_t size=0;
        const auto parsed=std::from_chars(count.data(),count.data()+count.size(),size);
        const bool directory=kind=="directory";
        if((kind!="file"&&!directory)||parsed.ec!=std::errc{}||parsed.ptr!=count.data()+count.size()||
            count!=std::to_string(size)||(directory?(size!=0||!digest.empty()):
                (digest.size()!=64||!std::ranges::all_of(digest,[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');})))||
            same_path(relative,std::filesystem::path(profile_migration_marker_name))||
            (!result.empty()&&result.back().path>=name))
            throw std::runtime_error("El inventario de migración contiene datos no admitidos.");
        result.push_back({name,directory,size,digest});
    }
    if(std::ranges::none_of(result,[](const auto& entry){return !entry.directory&&same_path(relative_path(entry.path),L"state.bin");}))
        throw std::runtime_error("El inventario de migración no contiene el estado original.");
    return result;
}
void validate_destination(const std::filesystem::path& profile,const Manifest* legacy){
    (void)state::StateStore(profile).load();const auto active=profile/L"state/active";
    // Inspect the full tree too: StateStore intentionally only opens state.bin.
    (void)scan(active,true);
    const auto marker=active/std::filesystem::path(profile_migration_marker_name);
    if(std::filesystem::exists(state::native_io_path(marker))){
        const auto recorded=read_marker(marker);
        if(legacy&&recorded!=*legacy)
            throw std::runtime_error("El progreso anterior cambió después de migrarlo. Se conservan ambos perfiles sin sustituirlos.");
    }else if(legacy)
        throw std::runtime_error("Existen dos perfiles con progreso y no hay una migración verificada entre ellos. Se conservan ambos sin elegir ni sobrescribir ninguno.");
}
}

ProfileMigrationResult migrate_profile_state(const std::filesystem::path& legacy_profile_root,
        const std::filesystem::path& new_profile_root)try{
    const auto legacy=profile_path(legacy_profile_root),destination=profile_path(new_profile_root);
    if(within(legacy,destination)||within(destination,legacy))
        throw std::runtime_error("Las carpetas anterior y nueva del progreso deben estar separadas.");
    const auto source=legacy/L"state/active",state_root=destination/L"state",target=state_root/L"active";
    state::require_safe_path(source);state::require_safe_path(target);
    validate_state_directory(legacy);validate_state_directory(destination);
    (void)state::StateStore(legacy).load();
    const bool has_source=std::filesystem::exists(state::native_io_path(source));
    const auto original=has_source?scan(source):Manifest{};
    if(std::filesystem::exists(state::native_io_path(target))){
        validate_destination(destination,has_source?&original:nullptr);
        return ProfileMigrationResult::destination_preserved;
    }
    // A missing active directory is an empty profile, not a new saved state.
    if(!has_source){(void)state::StateStore(destination).load();return ProfileMigrationResult::no_legacy_state;}
    make_directories(state_root);
    File lock{CreateFileW(state::native_io_path(state_root/L"writer.lock").c_str(),GENERIC_READ|GENERIC_WRITE,
        0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr)};
    if(lock.value==INVALID_HANDLE_VALUE)throw std::runtime_error("Otra instancia está guardando el perfil nuevo; no se ha migrado nada.");
    state::require_unredirected_handle(lock.value,state_root/L"writer.lock");
    if(std::filesystem::exists(state::native_io_path(target))){validate_destination(destination,&original);return ProfileMigrationResult::destination_preserved;}
    static std::atomic<unsigned long long> serial{0};
    const auto token=L"migration-"+std::to_wstring(GetCurrentProcessId())+L"-"+
        std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(++serial)+L".pending";
    const auto pending=state_root/token,pending_active=pending/L"state/active";
    if(!std::filesystem::create_directory(state::native_io_path(pending)))
        throw std::runtime_error("No se pudo reservar una carpeta nueva para la migración.");
    verify_directory(pending);make_directories(pending_active);
    for(const auto& entry:original){
        const auto relative=relative_path(entry.path),from=source/relative,to=pending_active/relative;
        if(entry.directory)make_directories(to);
        else{
            make_directories(to.parent_path());
            if(file_entry(from,entry.path,&to)!=entry)
                throw std::runtime_error("El progreso anterior cambió mientras se copiaba; se conserva sin publicar la copia parcial.");
        }
    }
    (void)state::StateStore(pending).load();
    if(scan(pending_active)!=original||scan(source)!=original)
        throw std::runtime_error("El inventario del progreso cambió durante la migración; se conservan los archivos originales.");
    write_marker(pending_active/std::filesystem::path(profile_migration_marker_name),original);
    if(read_marker(pending_active/std::filesystem::path(profile_migration_marker_name))!=original)
        throw std::runtime_error("No se pudo verificar el manifiesto de la copia pendiente.");
    if(!MoveFileExW(state::native_io_path(pending_active).c_str(),state::native_io_path(target).c_str(),MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("No se pudo publicar la migración; no se ha reemplazado ningún progreso.");
    // Only the now-empty directories created by this invocation are removed.
    std::error_code ignored;std::filesystem::remove(state::native_io_path(pending/L"state"),ignored);
    std::filesystem::remove(state::native_io_path(pending),ignored);
    return ProfileMigrationResult::migrated;
}catch(const std::filesystem::filesystem_error&){
    throw std::runtime_error("No se pudo acceder de forma segura a las carpetas del progreso. Los perfiles existentes se conservan.");
}

} // namespace lgaed::windows
