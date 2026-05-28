#include <ontology/Core.hpp>
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>

namespace ontology {

// ============================================================================
// Ontology 序列化
// ============================================================================

Json Ontology::toJson() const {
    Json j;
    j["id"] = id;
    j["name"] = name;
    j["description"] = description;
    j["version"] = version;
    j["baseIRI"] = baseIRI;

    // 类
    j["classes"] = Json::array();
    for (const auto& [id, cls] : classes) {
        Json c;
        c["id"] = cls.id;
        c["name"] = cls.name;
        c["description"] = cls.description;
        c["superClasses"] = cls.superClasses;
        c["equivalentClasses"] = cls.equivalentClasses;
        c["disjointClasses"] = cls.disjointClasses;
        c["properties"] = cls.properties;
        c["metadata"] = cls.metadata;
        j["classes"].push_back(c);
    }

    // 关系
    j["relations"] = Json::array();
    for (const auto& [id, rel] : relations) {
        Json r;
        r["id"] = rel.id;
        r["name"] = rel.name;
        r["description"] = rel.description;
        r["domain"] = rel.domain;
        r["range"] = rel.range;
        r["isFunctional"] = rel.isFunctional;
        r["isTransitive"] = rel.isTransitive;
        r["isSymmetric"] = rel.isSymmetric;
        r["isReflexive"] = rel.isReflexive;
        r["inverseProperty"] = rel.inverseProperty;
        r["superProperties"] = rel.superProperties;
        r["metadata"] = rel.metadata;
        j["relations"].push_back(r);
    }

    // 属性
    j["properties"] = Json::array();
    for (const auto& [id, prop] : properties) {
        Json p;
        p["id"] = prop.id;
        p["name"] = prop.name;
        p["description"] = prop.description;
        p["dataType"] = static_cast<int>(prop.dataType);
        p["customType"] = prop.customType;
        p["isRequired"] = prop.isRequired;
        p["isUnique"] = prop.isUnique;
        p["defaultValue"] = prop.defaultValue;
        p["enumValues"] = prop.enumValues;
        p["metadata"] = prop.metadata;
        j["properties"].push_back(p);
    }

    // 实例
    j["individuals"] = Json::array();
    for (const auto& [id, ind] : individuals) {
        Json i;
        i["id"] = ind.id;
        i["name"] = ind.name;
        i["classId"] = ind.classId;
        i["properties"] = ind.properties;

        Json rels;
        for (const auto& [relId, targets] : ind.relations) {
            rels[relId] = targets;
        }
        i["relations"] = rels;
        i["importance"] = ind.importance;
        i["metadata"] = ind.metadata;
        j["individuals"].push_back(i);
    }

    // 公理
    j["axioms"] = Json::array();
    for (const auto& [id, axiom] : axioms) {
        Json a;
        a["id"] = axiom.id;
        a["description"] = axiom.description;
        a["type"] = static_cast<int>(axiom.type);
        a["premise"] = axiom.premise;
        a["conclusion"] = axiom.conclusion;
        a["confidence"] = axiom.confidence;
        a["priority"] = axiom.priority;
        a["metadata"] = axiom.metadata;
        j["axioms"].push_back(a);
    }

    // 三元组
    j["triples"] = Json::array();
    for (const auto& t : triples) {
        j["triples"].push_back(t.toJson());
    }

    return j;
}

std::optional<Ontology> Ontology::fromJson(const Json& j) {
    try {
        Ontology onto;
        onto.id = j.value("id", "");
        onto.name = j.value("name", "");
        onto.description = j.value("description", "");
        onto.version = j.value("version", "1.0.0");
        onto.baseIRI = j.value("baseIRI", "");

        // 加载类
        if (j.contains("classes")) {
            for (const auto& c : j["classes"]) {
                Class cls;
                cls.id = c.value("id", "");
                cls.name = c.value("name", "");
                cls.description = c.value("description", "");
                cls.superClasses = c.value("superClasses", std::vector<String>{});
                cls.equivalentClasses = c.value("equivalentClasses", std::vector<String>{});
                cls.disjointClasses = c.value("disjointClasses", std::vector<String>{});
                cls.properties = c.value("properties", std::vector<String>{});
                cls.metadata = c.value("metadata", Json{});
                onto.classes[cls.id] = cls;
            }
        }

        // 加载关系
        if (j.contains("relations")) {
            for (const auto& r : j["relations"]) {
                Relation rel;
                rel.id = r.value("id", "");
                rel.name = r.value("name", "");
                rel.description = r.value("description", "");
                rel.domain = r.value("domain", "");
                rel.range = r.value("range", "");
                rel.isFunctional = r.value("isFunctional", false);
                rel.isTransitive = r.value("isTransitive", false);
                rel.isSymmetric = r.value("isSymmetric", false);
                rel.isReflexive = r.value("isReflexive", false);
                rel.inverseProperty = r.value("inverseProperty", "");
                rel.superProperties = r.value("superProperties", std::vector<String>{});
                rel.metadata = r.value("metadata", Json{});
                onto.relations[rel.id] = rel;
            }
        }

        // 加载属性
        if (j.contains("properties")) {
            for (const auto& p : j["properties"]) {
                Property prop;
                prop.id = p.value("id", "");
                prop.name = p.value("name", "");
                prop.description = p.value("description", "");
                prop.dataType = static_cast<Property::DataType>(p.value("dataType", 0));
                prop.customType = p.value("customType", "");
                prop.isRequired = p.value("isRequired", false);
                prop.isUnique = p.value("isUnique", false);
                prop.defaultValue = p.value("defaultValue", "");
                prop.enumValues = p.value("enumValues", std::vector<String>{});
                prop.metadata = p.value("metadata", Json{});
                onto.properties[prop.id] = prop;
            }
        }

        // 加载实例
        if (j.contains("individuals")) {
            for (const auto& i : j["individuals"]) {
                Individual ind;
                ind.id = i.value("id", "");
                ind.name = i.value("name", "");
                ind.classId = i.value("classId", "");
                ind.importance = i.value("importance", 1.0f);
                ind.metadata = i.value("metadata", Json{});

                if (i.contains("properties")) {
                    ind.properties = i["properties"];
                }

                if (i.contains("relations")) {
                    for (auto it = i["relations"].begin(); it != i["relations"].end(); ++it) {
                        std::vector<String> targets;
                        for (const auto& t : it.value()) {
                            targets.push_back(t.get<String>());
                        }
                        ind.relations[it.key()] = targets;
                    }
                }

                onto.individuals[ind.id] = ind;

                // 更新索引
                onto.classIndex[ind.classId].push_back(ind.id);
            }
        }

        // 加载公理
        if (j.contains("axioms")) {
            for (const auto& a : j["axioms"]) {
                Axiom axiom;
                axiom.id = a.value("id", "");
                axiom.description = a.value("description", "");
                axiom.type = static_cast<Axiom::Type>(a.value("type", 0));
                axiom.premise = a.value("premise", "");
                axiom.conclusion = a.value("conclusion", "");
                axiom.confidence = a.value("confidence", 1.0f);
                axiom.priority = a.value("priority", 1.0f);
                axiom.metadata = a.value("metadata", Json{});
                onto.axioms[axiom.id] = axiom;
            }
        }

        // 加载三元组
        if (j.contains("triples")) {
            for (const auto& t : j["triples"]) {
                Triple tr;
                tr.subject = t.value("subject", "");
                tr.predicate = t.value("predicate", "");
                tr.object = t.value("object", "");
                tr.isLiteral = t.value("isLiteral", false);
                tr.confidence = t.value("confidence", 1.0f);
                tr.weight = t.value("weight", 1.0f);
                tr.source = t.value("source", "");
                onto.triples.push_back(tr);

                // 更新索引
                size_t idx = onto.triples.size() - 1;
                onto.subjectIndex[tr.subject].push_back(idx);
                onto.predicateIndex[tr.predicate].push_back(idx);
                onto.objectIndex[tr.object].push_back(idx);
            }
        }

        return onto;
    } catch (const std::exception& e) {
        spdlog::error("Ontology core error: {}", e.what());
        return std::nullopt;
    }
}

} // namespace ontology
