#include "ontology/ApiHandler.hpp"

namespace ontology {

Json ApiHandler::classToJson(const Class& cls) {
    Json j;
    j["id"] = cls.id;
    j["name"] = cls.name;
    j["description"] = cls.description;
    j["superClasses"] = cls.superClasses;
    j["equivalentClasses"] = cls.equivalentClasses;
    j["disjointClasses"] = cls.disjointClasses;
    j["properties"] = cls.properties;
    // Temporal validity — only include when non-empty
    if (!cls.validFrom.empty()) j["validFrom"] = cls.validFrom;
    if (!cls.validTo.empty())   j["validTo"]   = cls.validTo;
    return j;
}

Json ApiHandler::relationToJson(const Relation& rel) {
    Json j;
    j["id"] = rel.id;
    j["name"] = rel.name;
    j["description"] = rel.description;
    j["domain"] = rel.domain;
    j["range"] = rel.range;
    j["isFunctional"] = rel.isFunctional;
    j["isInverseFunctional"] = rel.isInverseFunctional;
    j["isTransitive"] = rel.isTransitive;
    j["isSymmetric"] = rel.isSymmetric;
    return j;
}

Json ApiHandler::individualToJson(const Individual& ind) {
    Json j;
    j["id"] = ind.id;
    j["name"] = ind.name;
    j["classId"] = ind.classId;
    j["properties"] = ind.properties;

    Json rels;
    for (const auto& [relId, targets] : ind.relations) {
        rels[relId] = targets;
    }
    j["relations"] = rels;

    return j;
}

} // namespace ontology
