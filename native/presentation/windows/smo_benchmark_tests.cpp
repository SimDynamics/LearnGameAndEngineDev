#include "presentation/windows/smo_benchmark.hpp"
#include "core/smo.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <stop_token>
#include <string>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Action>
void rejects(Action action, const char* message) {
    try { action(); }
    catch (const std::invalid_argument&) { return; }
    throw std::runtime_error(message);
}

void print_samples(const char* label, const std::vector<double>& samples) {
    std::cout << label;
    for (const auto value : samples) std::cout << ' ' << value;
    std::cout << '\n';
}

bool valid_samples(const std::vector<double>& samples) {
    return std::ranges::all_of(samples,[](double value) {return std::isfinite(value)&&value>0;});
}

void particle_test(bool hardware) {
    using namespace lgaed::presentation;
    std::stop_source canceled;
    canceled.request_stop();
    bool stopped=false;
    try {(void)run_smo_particle_benchmark(GraphicsBackend::warp,false,canceled.get_token());}
    catch(const std::runtime_error& error) {stopped=std::string(error.what()).find("cancelada")!=std::string::npos;}
    require(stopped,"La prueba de partículas ignoró la cancelación previa.");
    const auto report=run_smo_particle_benchmark(hardware?GraphicsBackend::hardware:GraphicsBackend::warp,hardware);
    require(!report.backend.empty()&&report.software!=hardware,"La prueba de partículas ocultó su backend.");
    require(report.rows.size()==2,"Faltan los controles de grupo 1 y grupo 256.");
    require(report.target_ms>33&&report.target_ms<34,"El objetivo de carga no es un fotograma de 30 Hz.");
    require(report.stress==hardware,"El modo de verificación fue presentado como saturación física.");
    require(hardware?!report.calibration.empty():report.calibration.empty(),"Se confundieron calibración física y corrección WARP.");
    std::cout << std::setprecision(9) << "Partículas: " << report.backend << ", target_ms=" << report.target_ms << '\n';
    for(const auto& calibration:report.calibration) {
        require(calibration.particles>=65536&&calibration.particles<=2097152&&calibration.terms>=32&&calibration.terms<=8192,
            "La calibración sobrepasó sus límites reales.");
        require(calibration.timing_valid==(std::isfinite(calibration.naive_ms)&&calibration.naive_ms>0),"La calibración empleó un reloj no válido.");
        std::cout << "Calibración: particles=" << calibration.particles << ", terms=" << calibration.terms
            << ", naive_ms=" << calibration.naive_ms << ", timing_valid=" << calibration.timing_valid << '\n';
    }
    for(std::size_t stage=1;stage<report.calibration.size();++stage) {
        const auto& previous=report.calibration[stage-1];const auto& current=report.calibration[stage];
        require(previous.timing_valid&&previous.naive_ms<report.target_ms,
            "La calibración aumentó carga después de un objetivo alcanzado o una consulta inválida.");
        require((current.particles==previous.particles*2&&current.terms==previous.terms)||
            (previous.particles==2097152&&current.particles==previous.particles&&current.terms==previous.terms*2),
            "La calibración dio un salto de trabajo mayor que el doble o excedió el máximo de partículas.");
    }
    bool all_timed=std::ranges::all_of(report.calibration,[](const auto& entry) {return entry.timing_valid;});
    const unsigned rounds=hardware?9u:3u;
    for(std::size_t index=0;index<report.rows.size();++index) {
        const auto& row=report.rows[index];
        require(row.group_size==(index==0?1u:256u),"Falta el control sin ahorro o el caso agrupado.");
        require(row.groups==(row.particles+row.group_size-1)/row.group_size,"El conteo de emisores no coincide con las partículas.");
        require(row.naive_term_evaluations==static_cast<std::uint64_t>(row.particles)*row.terms&&
            row.grouped_term_evaluations==static_cast<std::uint64_t>(row.groups)*row.terms,"Se informó un conteo de términos inventado.");
        if(index==0)require(row.naive_term_evaluations==row.grouped_term_evaluations,"El grupo 1 anunció una reducción inexistente.");
        else require(row.grouped_term_evaluations*256==row.naive_term_evaluations,"La reducción por emisor no coincide con los términos evaluados.");
        if(!hardware)require(row.particles==1024&&row.terms==8,"WARP recibió una carga de saturación física.");
        require(row.values==static_cast<std::size_t>(row.particles)*4*3*rounds,"No se verificaron todas las salidas A/B/C de todas las rondas.");
        require(row.equivalent&&row.mismatches==0&&std::isfinite(row.max_error),"Las tres variantes de partículas no coinciden con el oráculo independiente.");
        require(row.naive_ms.size()==rounds&&row.grouped_ordinary_ms.size()==rounds&&row.grouped_smo_ms.size()==rounds,
            "Faltan muestras de un fotograma A/B/C.");
        require(row.timing_valid==(valid_samples(row.naive_ms)&&valid_samples(row.grouped_ordinary_ms)&&valid_samples(row.grouped_smo_ms)),
            "Se aceptaron consultas GPU disjuntas o ausentes.");
        if(row.timing_valid)require(std::isfinite(row.naive_median_ms)&&std::isfinite(row.grouped_ordinary_median_ms)&&
            std::isfinite(row.grouped_smo_median_ms),"Las medianas de partículas no son válidas.");
        else require(std::isnan(row.naive_median_ms)||std::isnan(row.grouped_ordinary_median_ms)||std::isnan(row.grouped_smo_median_ms),
            "Se reemplazó una consulta GPU inválida con tiempo CPU.");
        require(row.target_reached==(row.timing_valid&&row.naive_median_ms>=report.target_ms),"Se declaró alcanzada una carga que no se midió.");
        require(row.note.find("no representa el mejor algoritmo ordinario")!=std::string::npos,
            "El caso redundante fue presentado como el mejor algoritmo ordinario posible.");
        require(row.note.find("sin pausas de validación entre fotogramas")!=std::string::npos,
            "El informe ocultó interrupciones CPU entre los fotogramas de estrés.");
        all_timed=all_timed&&row.timing_valid;
        std::cout << "particles=" << row.particles << ", group_size=" << row.group_size << ", groups=" << row.groups
            << ", terms=" << row.terms << ", naive_terms=" << row.naive_term_evaluations << ", grouped_terms=" << row.grouped_term_evaluations
            << ", naive_median_ms=" << row.naive_median_ms << ", grouped_ordinary_median_ms=" << row.grouped_ordinary_median_ms
            << ", grouped_smo_median_ms=" << row.grouped_smo_median_ms << ", values=" << row.values << ", mismatches=" << row.mismatches
            << ", max_error=" << row.max_error << ", target_reached=" << row.target_reached << '\n';
        print_samples("naive_ms:",row.naive_ms);print_samples("grouped_ordinary_ms:",row.grouped_ordinary_ms);
        print_samples("grouped_smo_ms:",row.grouped_smo_ms);std::cout << row.note << '\n';
    }
    require(report.gpu_timestamps==all_timed,"El informe de partículas omitió consultas no válidas.");
    require(report.conclusion.find("no son FPS")!=std::string::npos&&report.conclusion.find("no un compilador GPU completo")!=std::string::npos&&
        report.conclusion.find("no demuestra una propiedad exclusiva")!=std::string::npos,"Se atribuyó una ventaja algorítmica a todo SMO.");
    if(!hardware) {
        require(report.conclusion.find("WARP/software")!=std::string::npos,"WARP se presentó como GPU física.");
        const auto downgraded=run_smo_particle_benchmark(GraphicsBackend::warp,true);
        require(downgraded.software&&!downgraded.stress&&downgraded.calibration.empty()&&downgraded.rows.size()==2,
            "Solicitar saturación WARP se presentó como estrés físico realmente ejecutado.");
        for(const auto& row:downgraded.rows)require(row.particles==1024&&row.terms==8&&row.equivalent,
            "Una petición de saturación WARP dejó de usar la carga pequeña comprobada.");
    }
    std::cout << report.conclusion << "\nPruebas de partículas SMO superadas.\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        using namespace lgaed::presentation;
        if(argc>1&&std::string(argv[1])=="particles") {
            const bool physical=argc>2&&std::string(argv[2])=="hardware";
            require(argc<=3&&(argc==2||physical),"Uso: lgaed_smo_benchmark_tests particles [hardware]");
            particle_test(physical);return 0;
        }
        const bool hardware = argc > 1 && std::string(argv[1]) == "hardware";
        if (argc > 1 && !hardware)
            throw std::invalid_argument("Uso: lgaed_smo_benchmark_tests [hardware [datos [rondas]]] | particles [hardware]");
        const auto count = hardware ? (argc > 2 ? static_cast<std::uint32_t>(std::stoul(argv[2])) : 65536u) : 1024u;
        const auto rounds = hardware ? (argc > 3 ? static_cast<unsigned>(std::stoul(argv[3])) : 9u) : 3u;
        require(argc <= 4, "Hay argumentos de prueba inesperados.");

        // Limits and cancellation are checked before any graphics device is
        // acquired, so rejected requests cannot leave a background GPU job.
        rejects([] { (void)run_smo_benchmark(GraphicsBackend::warp, 0, 3); }, "Se aceptó una carga vacía.");
        rejects([] { (void)run_smo_benchmark(GraphicsBackend::warp, 262145, 3); }, "Se aceptó una carga excesiva.");
        rejects([] { (void)run_smo_benchmark(GraphicsBackend::warp, 1024, 2); }, "Se aceptaron insuficientes rondas.");
        rejects([] { (void)run_smo_benchmark(GraphicsBackend::warp, 1024, 16); }, "Se aceptaron excesivas rondas.");
        std::stop_source stopped;
        stopped.request_stop();
        bool canceled = false;
        try { (void)run_smo_benchmark(GraphicsBackend::warp, 1024, 3, stopped.get_token()); }
        catch (const std::runtime_error& error) { canceled = std::string(error.what()).find("cancelada") != std::string::npos; }
        require(canceled, "Una prueba cancelada alcanzó el dispositivo GPU.");

        // These same boundary inputs reach the real compute shader and are
        // checked against core::multiply by the untimed oracle, not a second
        // assumed implementation of the SMO law.
        require(lgaed::smo::multiply(0, 2).value == 0&&lgaed::smo::multiply(0,2).alternative==2, "Se perdió el cero predeterminado o la cantidad alternativa.");
        require(lgaed::smo::multiply(-2, -3).value == -6, "Cambió la regla SMO de signos.");
        const auto report = run_smo_benchmark(hardware ? GraphicsBackend::hardware : GraphicsBackend::warp, count, rounds);
        require(!report.backend.empty(), "Falta el nombre del dispositivo real.");
        require(hardware ? !report.software : report.software, "No se respetó el tipo de dispositivo solicitado.");
        require(report.rows.size() == 5, "Faltan escenarios reales de cómputo.");
        require(report.dispatches_per_sample==64,"No se identificó el lote real medido por las consultas GPU.");
        bool all_timed = true;
        std::cout << std::setprecision(9) << report.backend << ", inputs="<<count<<", rounds="<<rounds<<", dispatches_per_sample="<<report.dispatches_per_sample<<'\n';
        for (std::size_t i = 0; i < report.rows.size(); ++i) {
            const auto& row = report.rows[i];
            require(!row.name.empty() && !row.note.empty(), "Falta identificar un escenario o su alcance.");
            require(row.values == static_cast<std::size_t>(count) * 4, "No se verificaron todas las salidas.");
            require(row.ordinary_ms.size() == rounds && row.smo_ms.size() == rounds, "Faltan muestras emparejadas.");
            require(row.equivalent == (row.mismatches == 0), "La equivalencia oculta diferencias de resultados.");
            require(std::isfinite(row.max_error) && row.max_error >= 0, "El error de resultados no es válido.");
            if (i < 3) require(row.equivalent, "Un escenario equivalente alteró el problema original.");
            else require(!row.equivalent && row.max_error > 0, "Se ocultaron diferencias de las reglas de cero/signos.");
            const bool positive_samples = std::ranges::all_of(row.ordinary_ms, [](double value) { return std::isfinite(value) && value > 0; }) &&
                std::ranges::all_of(row.smo_ms, [](double value) { return std::isfinite(value) && value > 0; });
            require(row.timing_valid == positive_samples, "Se declararon válidos tiempos GPU ausentes o disjuntos.");
            if (row.timing_valid) {
                require(std::isfinite(row.ordinary_median_ms) && row.ordinary_median_ms > 0 &&
                    std::isfinite(row.smo_median_ms) && row.smo_median_ms > 0, "Las medianas GPU no son válidas.");
            } else {
                require(std::isnan(row.ordinary_median_ms) || std::isnan(row.smo_median_ms), "Un reloj CPU sustituyó una consulta GPU inválida.");
            }
            all_timed = all_timed && row.timing_valid;
            std::cout << row.name << ": equivalent=" << row.equivalent << ", values=" << row.values
                << ", mismatches=" << row.mismatches << ", max_error=" << row.max_error
                << ", ordinary_median_ms=" << row.ordinary_median_ms << ", smo_median_ms=" << row.smo_median_ms
                << ", timing_valid=" << row.timing_valid << '\n';
            print_samples("ordinary_ms:", row.ordinary_ms);
            print_samples("smo_ms:", row.smo_ms);
            std::cout << row.note << '\n';
        }
        require(report.gpu_timestamps == all_timed, "El informe no distingue consultas GPU inválidas.");
        require(report.conclusion.find("HLSL") != std::string::npos &&
            report.conclusion.find("no es un compilador GPU") != std::string::npos,
            "El informe atribuye al lenguaje una comparación de kernels manuales.");
        if (!hardware) require(report.conclusion.find("WARP") != std::string::npos, "WARP se presentó como GPU física.");
        std::cout << report.conclusion << "\nPruebas SMO GPU superadas.\n";
        if(!hardware)particle_test(false);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Fallo de prueba SMO GPU: " << error.what() << '\n';
        return 1;
    }
}
