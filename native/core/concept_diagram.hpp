#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace lgaed::curriculum {
// Reading-only authored flow. No execution, response, or simulated state.
struct ConceptDiagramNode {
    std::string id, kind, label, value, caption;
    bool operator==(const ConceptDiagramNode&) const = default;
};
struct ConceptDiagramLink {
    std::string from, to, label;
    bool operator==(const ConceptDiagramLink&) const = default;
};
struct ConceptDiagramField {
    std::string key, path, text;
};
struct ConceptDiagramSpec {
    std::string title;
    std::vector<ConceptDiagramNode> nodes;
    std::vector<ConceptDiagramLink> links;
    bool operator==(const ConceptDiagramSpec&) const = default;

    [[nodiscard]] ConceptDiagramField title_field() const {
        return {"concept-diagram/title", "/title", title};
    }
    [[nodiscard]] std::vector<ConceptDiagramField> node_fields(std::size_t index) const {
        const auto& node=nodes.at(index);
        const auto prefix="concept-diagram/node/"+node.id;
        const auto path="/nodes/"+std::to_string(index);
        std::vector<ConceptDiagramField> fields{{prefix+"/label",path+"/label",node.label},
            {prefix+"/value",path+"/value",node.value}};
        if(!node.caption.empty())fields.push_back({prefix+"/caption",path+"/caption",node.caption});
        return fields;
    }
    [[nodiscard]] std::optional<ConceptDiagramField> link_field(std::size_t index) const {
        const auto& link=links.at(index);
        if(link.label.empty())return {};
        return ConceptDiagramField{"concept-diagram/link/"+link.from+"/"+link.to+"/label",
            "/links/"+std::to_string(index)+"/label",link.label};
    }
    [[nodiscard]] std::vector<ConceptDiagramField> reading_fields() const {
        std::vector<ConceptDiagramField> fields{title_field()};
        for(std::size_t n=0;n<nodes.size();++n){
            const auto node=node_fields(n);
            fields.insert(fields.end(),node.begin(),node.end());
            if(n<links.size())if(const auto link=link_field(n))fields.push_back(*link);
        }
        return fields;
    }
};
} // namespace lgaed::curriculum
