#include <ontology/ShaclValidation.hpp>
#include <ontology/Temporal.hpp>
#include <algorithm>
#include <sstream>
#include <regex>
#include <unordered_set>

namespace ontology {

// ============================================================================
// ShaclValidator
// ============================================================================

ShaclValidator::ShaclValidator(StoragePtr storage) : storage_(storage) {}

void ShaclValidator::addShape(const ShaclNodeShape& shape) {
    std::lock_guard<std::mutex> lock(mutex_);
    shapes_[shape.id] = shape;
}

void ShaclValidator::removeShape(const String& shapeId) {
    std::lock_guard<std::mutex> lock(mutex_);
    shapes_.erase(shapeId);
}

std::vector<ShaclNodeShape> ShaclValidator::getShapes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ShaclNodeShape> result;
    for (const auto& [id, shape] : shapes_) {
        result.push_back(shape);
    }
    return result;
}

void ShaclValidator::loadShapesFromJson(const Json& shapesJson) {
    if (!shapesJson.is_array()) return;

    for (const auto& sj : shapesJson) {
        auto shape = ShaclNodeShape::fromJson(sj);
        if (!shape.id.empty()) {
            addShape(shape);
        }
    }
}

ShaclValidationReport ShaclValidator::validate() {
    ShaclValidationReport report;

    auto individuals = storage_->getAllIndividuals();
    for (const auto& ind : individuals) {
        auto nodeReport = validateNode(ind.id);
        report.conforms = report.conforms && nodeReport.conforms;
        report.results.insert(report.results.end(),
            nodeReport.results.begin(), nodeReport.results.end());
    }

    return report;
}

ShaclValidationReport ShaclValidator::validateNode(const String& nodeId) {
    ShaclValidationReport report;
    report.conforms = true;

    auto ind = storage_->getIndividual(nodeId);
    if (!ind) return report;

    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& [shapeId, shape] : shapes_) {
        if (shape.deactivated) continue;

        // Check if this shape applies to this node
        bool applies = false;

        // sh:targetClass
        if (!shape.targetClass.empty() && ind->classId == shape.targetClass) {
            applies = true;
        }
        // Check superclasses
        if (!shape.targetClass.empty() && !applies) {
            // Get all types of the individual
            auto bySubject = storage_->findBySubject(nodeId);
            for (const auto& t : bySubject) {
                if (t.predicate == "rdf:type" && t.object == shape.targetClass) {
                    applies = true;
                    break;
                }
                if (t.predicate == "subClassOf" && t.object == shape.targetClass) {
                    applies = true;
                    break;
                }
            }
        }

        // sh:targetNode
        for (const auto& tn : shape.targetNodes) {
            if (tn == nodeId) { applies = true; break; }
        }

        // sh:targetSubjectsOf
        for (const auto& pred : shape.targetSubjectsOf) {
            auto values = getPropertyValues(nodeId, pred);
            if (!values.empty()) { applies = true; break; }
        }

        // sh:targetObjectsOf
        if (!applies) {
            for (const auto& pred : shape.targetObjectsOf) {
                auto byObject = storage_->findByObject(nodeId);
                for (const auto& t : byObject) {
                    if (t.predicate == pred) { applies = true; break; }
                }
                if (applies) break;
            }
        }

        if (!applies) continue;

        // Validate each property constraint
        for (const auto& property : shape.properties) {
            auto results = validateProperty(nodeId, property, shape);
            for (const auto& result : results) {
                if (result.severity != Severity::Info || !result.message.empty()) {
                    report.conforms = false;
                    report.results.push_back(result);
                }
            }
        }

        // Validate closed shape
        if (shape.closed) {
            auto closedResults = validateClosed(nodeId, shape);
            for (auto& r : closedResults) {
                report.conforms = false;
                report.results.push_back(r);
            }
        }

        // Validate logical constraints (and/or/not/xone)
        auto logicalResults = validateLogicalConstraints(nodeId, shape);
        for (const auto& r : logicalResults) {
            report.conforms = false;
            report.results.push_back(r);
        }
    }

    return report;
}

ShaclValidationReport ShaclValidator::validateClass(const String& classId) {
    ShaclValidationReport report;
    report.conforms = true;

    auto individuals = storage_->getIndividualsByClass(classId);
    for (const auto& ind : individuals) {
        auto nodeReport = validateNode(ind.id);
        report.conforms = report.conforms && nodeReport.conforms;
        report.results.insert(report.results.end(),
            nodeReport.results.begin(), nodeReport.results.end());
    }

    return report;
}

ShaclValidationReport ShaclValidator::validateIncremental(
    const std::vector<Triple>& addedTriples,
    const std::vector<Triple>& removedTriples
) {
    auto affectedNodes = getAffectedNodes(addedTriples, removedTriples);

    ShaclValidationReport report;
    report.conforms = true;

    for (const auto& nodeId : affectedNodes) {
        auto nodeReport = validateNode(nodeId);
        report.conforms = report.conforms && nodeReport.conforms;
        report.results.insert(report.results.end(),
            nodeReport.results.begin(), nodeReport.results.end());
    }

    return report;
}

// ============================================================================
// Property Validation
// ============================================================================

std::vector<ShaclValidationResult> ShaclValidator::validateProperty(
    const String& focusNode,
    const ShaclPropertyShape& property,
    const ShaclNodeShape& shape
) {
    std::vector<ShaclValidationResult> allResults;

    // Cardinality
    auto cardinalityResults = validateCardinality(focusNode, property);
    allResults.insert(allResults.end(), cardinalityResults.begin(), cardinalityResults.end());

    // Value type
    auto typeResults = validateValueType(focusNode, property);
    allResults.insert(allResults.end(), typeResults.begin(), typeResults.end());

    // Range
    auto rangeResults = validateRange(focusNode, property);
    allResults.insert(allResults.end(), rangeResults.begin(), rangeResults.end());

    // Pattern
    auto patternResults = validatePattern(focusNode, property);
    allResults.insert(allResults.end(), patternResults.begin(), patternResults.end());

    // Qualified value shape
    auto qualifiedResults = validateQualifiedValueShape(focusNode, property);
    allResults.insert(allResults.end(), qualifiedResults.begin(), qualifiedResults.end());

    return allResults;
}

std::vector<ShaclValidationResult> ShaclValidator::validateCardinality(
    const String& focusNode,
    const ShaclPropertyShape& property
) {
    std::vector<ShaclValidationResult> results;
    auto values = getPropertyValues(focusNode, property.path);
    int count = static_cast<int>(values.size());

    if (property.minCount >= 0 && count < property.minCount) {
        ShaclValidationResult r;
        r.shapeId = "";
        r.constraintId = property.id;
        r.focusNode = focusNode;
        r.path = property.path;
        r.message = "Property " + property.path + " has " + std::to_string(count) +
                    " values, but minimum " + std::to_string(property.minCount) + " required";
        r.severity = property.severity;
        results.push_back(r);
    }

    if (property.maxCount >= 0 && count > property.maxCount) {
        ShaclValidationResult r;
        r.shapeId = "";
        r.constraintId = property.id;
        r.focusNode = focusNode;
        r.path = property.path;
        r.message = "Property " + property.path + " has " + std::to_string(count) +
                    " values, but maximum " + std::to_string(property.maxCount) + " allowed";
        r.severity = property.severity;
        results.push_back(r);
    }

    return results;
}

std::vector<ShaclValidationResult> ShaclValidator::validateValueType(
    const String& focusNode,
    const ShaclPropertyShape& property
) {
    std::vector<ShaclValidationResult> results;
    auto values = getPropertyValues(focusNode, property.path);

    // sh:class — all values must be instances of the specified class
    if (!property.classId.empty()) {
        for (const auto& val : values) {
            auto ind = storage_->getIndividual(val);
            if (!ind) {
                // Check if it's a literal (skip class check for literals)
                continue;
            }

            if (ind->classId != property.classId) {
                // Check if the individual's class is a subclass
                bool isSubClass = false;
                auto cls = storage_->getClass(ind->classId);
                while (cls) {
                    for (const auto& sc : cls->superClasses) {
                        if (sc == property.classId) {
                            isSubClass = true;
                            break;
                        }
                    }
                    if (isSubClass) break;
                    if (!cls->superClasses.empty()) {
                        cls = storage_->getClass(cls->superClasses[0]);
                    } else {
                        break;
                    }
                }

                if (!isSubClass) {
                    ShaclValidationResult r;
                    r.constraintId = property.id;
                    r.focusNode = focusNode;
                    r.path = property.path;
                    r.value = val;
                    r.message = "Value " + val + " is not an instance of class " + property.classId;
                    r.severity = property.severity;
                    results.push_back(r);
                }
            }
        }
    }

    // sh:in — value must be one of the enumeration
    if (!property.inValues.empty()) {
        for (const auto& val : values) {
            if (std::find(property.inValues.begin(), property.inValues.end(), val) ==
                property.inValues.end()) {
                ShaclValidationResult r;
                r.constraintId = property.id;
                r.focusNode = focusNode;
                r.path = property.path;
                r.value = val;
                r.message = "Value " + val + " is not in the allowed enumeration";
                r.severity = property.severity;
                results.push_back(r);
            }
        }
    }

    // sh:nodeKind
    if (!property.nodeKind.empty()) {
        for (const auto& val : values) {
            bool isIRI = !val.empty() && val.find("://") != String::npos;
            bool isLiteral = !isIRI;

            bool valid = true;
            if (property.nodeKind == "IRI" && !isIRI) valid = false;
            else if (property.nodeKind == "Literal" && !isLiteral) valid = false;

            if (!valid) {
                ShaclValidationResult r;
                r.constraintId = property.id;
                r.focusNode = focusNode;
                r.path = property.path;
                r.value = val;
                r.message = "Value " + val + " does not match nodeKind " + property.nodeKind;
                r.severity = property.severity;
                results.push_back(r);
            }
        }
    }

    // sh:hasValue — the property must have at least one value equal to fixedValue
    if (property.hasValue) {
        bool found = false;
        for (const auto& val : values) {
            if (val == property.fixedValue) {
                found = true;
                break;
            }
        }
        if (!found) {
            ShaclValidationResult r;
            r.constraintId = property.id;
            r.focusNode = focusNode;
            r.path = property.path;
            r.value = property.fixedValue;
            r.message = "Property " + property.path + " does not have required value " + property.fixedValue;
            r.severity = property.severity;
            results.push_back(r);
        }
    }

    // sh:datatype — check if values match the expected datatype
    if (!property.datatype.empty()) {
        for (const auto& val : values) {
            bool matches = false;
            if (property.datatype == "http://www.w3.org/2001/XMLSchema#string") {
                matches = true; // all values are strings in our model
            } else if (property.datatype == "http://www.w3.org/2001/XMLSchema#integer" ||
                       property.datatype == "http://www.w3.org/2001/XMLSchema#int") {
                try { std::stoi(val); matches = true; } catch (...) {}
            } else if (property.datatype == "http://www.w3.org/2001/XMLSchema#decimal" ||
                       property.datatype == "http://www.w3.org/2001/XMLSchema#double" ||
                       property.datatype == "http://www.w3.org/2001/XMLSchema#float") {
                try { std::stod(val); matches = true; } catch (...) {}
            } else if (property.datatype == "http://www.w3.org/2001/XMLSchema#boolean") {
                matches = (val == "true" || val == "false" || val == "1" || val == "0");
            } else {
                matches = true; // Unknown datatype, assume match
            }
            if (!matches) {
                ShaclValidationResult r;
                r.constraintId = property.id;
                r.focusNode = focusNode;
                r.path = property.path;
                r.value = val;
                r.message = "Value " + val + " does not match datatype " + property.datatype;
                r.severity = property.severity;
                results.push_back(r);
            }
        }
    }

    // sh:uniqueLang — no two values of the property should share the same language tag
    if (property.uniqueLang) {
        std::unordered_set<String> seenLangs;
        for (const auto& val : values) {
            // Check if value has a language tag (stored as value@lang in our model)
            auto atPos = val.find('@');
            if (atPos != String::npos && atPos + 1 < val.size()) {
                String lang = val.substr(atPos + 1);
                if (seenLangs.count(lang)) {
                    ShaclValidationResult r;
                    r.constraintId = property.id;
                    r.focusNode = focusNode;
                    r.path = property.path;
                    r.value = val;
                    r.message = "Duplicate language tag " + lang + " for property " + property.path;
                    r.severity = property.severity;
                    results.push_back(r);
                }
                seenLangs.insert(lang);
            }
        }
    }

    return results;
}

std::vector<ShaclValidationResult> ShaclValidator::validateRange(
    const String& focusNode,
    const ShaclPropertyShape& property
) {
    std::vector<ShaclValidationResult> results;
    auto values = getPropertyValues(focusNode, property.path);

    for (const auto& val : values) {
        // Numeric range
        if (property.minValue.has_value() || property.maxValue.has_value()) {
            try {
                double numVal = std::stod(val);
                if (property.minValue.has_value() && numVal < property.minValue.value()) {
                    ShaclValidationResult r;
                    r.constraintId = property.id;
                    r.focusNode = focusNode;
                    r.path = property.path;
                    r.value = val;
                    r.message = "Value " + val + " is less than minimum " +
                                std::to_string(property.minValue.value());
                    r.severity = property.severity;
                    results.push_back(r);
                }
                if (property.maxValue.has_value() && numVal > property.maxValue.value()) {
                    ShaclValidationResult r;
                    r.constraintId = property.id;
                    r.focusNode = focusNode;
                    r.path = property.path;
                    r.value = val;
                    r.message = "Value " + val + " is greater than maximum " +
                                std::to_string(property.maxValue.value());
                    r.severity = property.severity;
                    results.push_back(r);
                }
                // sh:minExclusive (strict inequality)
                if (property.exclusiveMinValue.has_value() && numVal <= property.exclusiveMinValue.value()) {
                    ShaclValidationResult r;
                    r.constraintId = property.id;
                    r.focusNode = focusNode;
                    r.path = property.path;
                    r.value = val;
                    r.message = "Value " + val + " is not greater than (exclusive) " +
                                std::to_string(property.exclusiveMinValue.value());
                    r.severity = property.severity;
                    results.push_back(r);
                }
                // sh:maxExclusive (strict inequality)
                if (property.exclusiveMaxValue.has_value() && numVal >= property.exclusiveMaxValue.value()) {
                    ShaclValidationResult r;
                    r.constraintId = property.id;
                    r.focusNode = focusNode;
                    r.path = property.path;
                    r.value = val;
                    r.message = "Value " + val + " is not less than (exclusive) " +
                                std::to_string(property.exclusiveMaxValue.value());
                    r.severity = property.severity;
                    results.push_back(r);
                }
            } catch (...) {
                // Not a numeric value, skip
            }
        }

        // String length
        if (property.minLength >= 0 && static_cast<int>(val.length()) < property.minLength) {
            ShaclValidationResult r;
            r.constraintId = property.id;
            r.focusNode = focusNode;
            r.path = property.path;
            r.value = val;
            r.message = "Value length " + std::to_string(val.length()) +
                        " is less than minimum " + std::to_string(property.minLength);
            r.severity = property.severity;
            results.push_back(r);
        }
        if (property.maxLength >= 0 && static_cast<int>(val.length()) > property.maxLength) {
            ShaclValidationResult r;
            r.constraintId = property.id;
            r.focusNode = focusNode;
            r.path = property.path;
            r.value = val;
            r.message = "Value length " + std::to_string(val.length()) +
                        " exceeds maximum " + std::to_string(property.maxLength);
            r.severity = property.severity;
            results.push_back(r);
        }
    }

    return results;
}

std::vector<ShaclValidationResult> ShaclValidator::validatePattern(
    const String& focusNode,
    const ShaclPropertyShape& property
) {
    std::vector<ShaclValidationResult> results;

    if (property.pattern.empty()) return results;

    auto values = getPropertyValues(focusNode, property.path);

    try {
        std::regex re(property.pattern);
        for (const auto& val : values) {
            if (!std::regex_search(val, re)) {
                ShaclValidationResult r;
                r.constraintId = property.id;
                r.focusNode = focusNode;
                r.path = property.path;
                r.value = val;
                r.message = "Value " + val + " does not match pattern " + property.pattern;
                r.severity = property.severity;
                results.push_back(r);
            }
        }
    } catch (const std::regex_error&) {
        // Invalid pattern, skip
    }

    return results;
}

std::vector<ShaclValidationResult> ShaclValidator::validateClosed(
    const String& focusNode,
    const ShaclNodeShape& shape
) {
    std::vector<ShaclValidationResult> results;

    // Collect allowed properties
    std::unordered_set<String> allowedPaths;
    for (const auto& prop : shape.properties) {
        allowedPaths.insert(prop.path);
    }
    for (const auto& ip : shape.ignoredProperties) {
        allowedPaths.insert(ip);
    }

    // Get all properties of the node
    auto triples = storage_->findBySubject(focusNode);
    for (const auto& t : triples) {
        if (t.predicate == "rdf:type" || t.predicate == "subClassOf") continue;
        if (allowedPaths.find(t.predicate) == allowedPaths.end()) {
            ShaclValidationResult r;
            r.shapeId = shape.id;
            r.focusNode = focusNode;
            r.path = t.predicate;
            r.value = t.object;
            r.message = "Property " + t.predicate + " is not allowed on closed shape " + shape.id;
            r.severity = shape.severity;
            results.push_back(r);
        }
    }

    return results;
}

std::vector<ShaclValidationResult> ShaclValidator::validateQualifiedValueShape(
    const String& focusNode,
    const ShaclPropertyShape& property
) {
    std::vector<ShaclValidationResult> results;
    if (property.qualifiedValueShapes.empty()) return results;

    auto values = getPropertyValues(focusNode, property.path);

    // For each qualifiedValueShape, count how many values conform
    for (const auto& qualShapeId : property.qualifiedValueShapes) {
        int conformingCount = 0;

        // Find the referenced shape
        auto shapeIt = shapes_.find(qualShapeId);
        if (shapeIt == shapes_.end()) continue;

        const auto& qualShape = shapeIt->second;

        for (const auto& val : values) {
            // Check if the value node conforms to the qualified shape
            auto valReport = validateNode(val);
            bool conforms = valReport.conforms;

            // Also check property constraints of the qualified shape
            for (const auto& prop : qualShape.properties) {
                auto propResults = validateProperty(val, prop, qualShape);
                if (!propResults.empty()) {
                    conforms = false;
                    break;
                }
            }

            if (conforms) conformingCount++;
        }

        if (property.qualifiedMinCount >= 0 && conformingCount < property.qualifiedMinCount) {
            ShaclValidationResult r;
            r.constraintId = property.id;
            r.focusNode = focusNode;
            r.path = property.path;
            r.message = "Qualified value shape " + qualShapeId +
                        ": found " + std::to_string(conformingCount) +
                        " conforming values, minimum " + std::to_string(property.qualifiedMinCount) + " required";
            r.severity = property.severity;
            results.push_back(r);
        }

        if (property.qualifiedMaxCount >= 0 && conformingCount > property.qualifiedMaxCount) {
            ShaclValidationResult r;
            r.constraintId = property.id;
            r.focusNode = focusNode;
            r.path = property.path;
            r.message = "Qualified value shape " + qualShapeId +
                        ": found " + std::to_string(conformingCount) +
                        " conforming values, maximum " + std::to_string(property.qualifiedMaxCount) + " allowed";
            r.severity = property.severity;
            results.push_back(r);
        }
    }

    return results;
}

std::vector<ShaclValidationResult> ShaclValidator::validateLogicalConstraints(
    const String& focusNode,
    const ShaclNodeShape& shape
) {
    std::vector<ShaclValidationResult> results;

    // sh:and — all sub-shapes must pass
    if (!shape.andShapes.empty()) {
        bool allPass = true;
        for (const auto& subShapeId : shape.andShapes) {
            auto it = shapes_.find(subShapeId);
            if (it == shapes_.end()) continue;
            // Check if this sub-shape's properties all pass
            bool thisPass = true;
            for (const auto& prop : it->second.properties) {
                auto propResults = validateProperty(focusNode, prop, it->second);
                if (!propResults.empty()) {
                    thisPass = false;
                    break;
                }
            }
            if (!thisPass) {
                allPass = false;
                break;
            }
        }
        if (!allPass) {
            ShaclValidationResult r;
            r.shapeId = shape.id;
            r.focusNode = focusNode;
            r.message = "Node " + focusNode + " does not conform to all shapes in sh:and";
            r.severity = shape.severity;
            results.push_back(r);
        }
    }

    // sh:or — at least one sub-shape must pass
    if (!shape.orShapes.empty()) {
        bool anyPass = false;
        for (const auto& subShapeId : shape.orShapes) {
            auto it = shapes_.find(subShapeId);
            if (it == shapes_.end()) continue;
            // Check if this sub-shape's properties all pass
            bool thisPass = true;
            for (const auto& prop : it->second.properties) {
                auto propResults = validateProperty(focusNode, prop, it->second);
                if (!propResults.empty()) {
                    thisPass = false;
                    break;
                }
            }
            if (thisPass) { anyPass = true; break; }
        }
        if (!anyPass) {
            ShaclValidationResult r;
            r.shapeId = shape.id;
            r.focusNode = focusNode;
            r.message = "Node " + focusNode + " does not conform to any shape in sh:or";
            r.severity = shape.severity;
            results.push_back(r);
        }
    }

    // sh:not — the negated shape must NOT pass
    if (!shape.notShape.empty()) {
        auto it = shapes_.find(shape.notShape);
        if (it != shapes_.end()) {
            bool passes = true;
            for (const auto& prop : it->second.properties) {
                auto propResults = validateProperty(focusNode, prop, it->second);
                if (!propResults.empty()) {
                    passes = false;
                    break;
                }
            }
            if (passes) {
                ShaclValidationResult r;
                r.shapeId = shape.id;
                r.focusNode = focusNode;
                r.message = "Node " + focusNode + " conforms to negated shape " + shape.notShape;
                r.severity = shape.severity;
                results.push_back(r);
            }
        }
    }

    // sh:xone — exactly one sub-shape must pass
    if (!shape.xoneShape.empty()) {
        int passCount = 0;
        std::istringstream iss(shape.xoneShape);
        String shapeId;
        while (std::getline(iss, shapeId, ',')) {
            // Trim whitespace
            while (!shapeId.empty() && std::isspace(static_cast<unsigned char>(shapeId.front()))) shapeId.erase(shapeId.begin());
            while (!shapeId.empty() && std::isspace(static_cast<unsigned char>(shapeId.back()))) shapeId.pop_back();

            auto it = shapes_.find(shapeId);
            if (it == shapes_.end()) continue;
            bool passes = true;
            for (const auto& prop : it->second.properties) {
                auto propResults = validateProperty(focusNode, prop, it->second);
                if (!propResults.empty()) { passes = false; break; }
            }
            if (passes) passCount++;
        }
        if (passCount != 1) {
            ShaclValidationResult r;
            r.shapeId = shape.id;
            r.focusNode = focusNode;
            r.message = "Node " + focusNode + " conforms to " + std::to_string(passCount) +
                        " shapes in sh:xone, expected exactly 1";
            r.severity = shape.severity;
            results.push_back(r);
        }
    }

    return results;
}

std::vector<String> ShaclValidator::getPropertyValues(
    const String& nodeId,
    const String& propertyPath
) const {
    std::vector<String> values;

    // Direct triple lookup
    auto triples = storage_->findBySP(nodeId, propertyPath);
    for (const auto& t : triples) {
        values.push_back(t.object);
    }

    // Also check Individual's properties map
    auto ind = storage_->getIndividual(nodeId);
    if (ind) {
        auto it = ind->relations.find(propertyPath);
        if (it != ind->relations.end()) {
            for (const auto& v : it->second) {
                if (std::find(values.begin(), values.end(), v) == values.end()) {
                    values.push_back(v);
                }
            }
        }

        auto pit = ind->properties.find(propertyPath);
        if (pit != ind->properties.end()) {
            values.push_back(pit->second.dump());
        }
    }

    return values;
}

std::vector<String> ShaclValidator::getAffectedNodes(
    const std::vector<Triple>& added,
    const std::vector<Triple>& removed
) const {
    std::unordered_set<String> nodes;

    for (const auto& t : added) {
        nodes.insert(t.subject);
        nodes.insert(t.object);
    }
    for (const auto& t : removed) {
        nodes.insert(t.subject);
        nodes.insert(t.object);
    }

    return std::vector<String>(nodes.begin(), nodes.end());
}

// ============================================================================
// IncrementalReasoner
// ============================================================================

IncrementalReasoner::IncrementalReasoner(
    StoragePtr storage,
    std::shared_ptr<SymbolicReasoner> symbolic,
    std::shared_ptr<ShaclValidator> shaclValidator
)
    : storage_(storage)
    , symbolic_(symbolic)
    , shaclValidator_(shaclValidator)
{
}

IncrementalReasoner::InferenceDelta
IncrementalReasoner::processChange(
    const std::vector<Triple>& addedTriples,
    const std::vector<Triple>& removedTriples
) {
    InferenceDelta delta;

    // Step 1: Apply additions
    for (const auto& t : addedTriples) {
        if (storage_) storage_->addTriple(t);
    }

    // Step 2: Forward chain on added triples
    if (config_.enableForwardChaining && symbolic_) {
        auto inferred = forwardChain(addedTriples);
        for (const auto& t : inferred) {
            delta.addedFacts.push_back(t);
            if (storage_) storage_->addTriple(t);
        }
    }

    // Step 3: Retract based on removed triples
    auto retracted = retract(removedTriples);
    delta.removedFacts = retracted;

    // Step 4: Apply removals
    for (const auto& t : removedTriples) {
        if (storage_) storage_->removeTriple(t);
    }
    for (const auto& t : retracted) {
        if (storage_) storage_->removeTriple(t);
    }

    // Step 5: SHACL validation
    if (config_.validateWithShacl && shaclValidator_) {
        auto report = shaclValidator_->validateIncremental(addedTriples, removedTriples);
        delta.violations = report.results;
    }

    // Step 6: Update cache
    for (const auto& t : addedTriples) {
        invalidateCache(t.subject, t.predicate);
    }
    for (const auto& t : removedTriples) {
        invalidateCache(t.subject, t.predicate);
    }

    // Metadata
    delta.metadata = {
        {"addedInput", static_cast<int>(addedTriples.size())},
        {"removedInput", static_cast<int>(removedTriples.size())},
        {"addedInferred", static_cast<int>(delta.addedFacts.size())},
        {"removedInferred", static_cast<int>(delta.removedFacts.size())},
        {"violations", static_cast<int>(delta.violations.size())}
    };

    return delta;
}

IncrementalReasoner::InferenceDelta
IncrementalReasoner::addTriple(const Triple& triple) {
    return processChange({triple}, {});
}

IncrementalReasoner::InferenceDelta
IncrementalReasoner::removeTriple(const Triple& triple) {
    return processChange({}, {triple});
}

std::vector<Triple> IncrementalReasoner::forwardChain(
    const std::vector<Triple>& triggers
) {
    std::vector<Triple> inferred;
    std::unordered_set<String> seen;

    // Apply transitive relations
    for (const auto& t : triggers) {
        String triggerKey = t.subject + ":" + t.predicate + ":" + t.object;
        auto rel = storage_->getRelation(t.predicate);
        if (rel && rel->isTransitive) {
            // Find objects that are connected via the same transitive relation
            auto objTriples = storage_->findBySP(t.object, t.predicate);
            for (const auto& ot : objTriples) {
                Triple newT;
                newT.subject = t.subject;
                newT.predicate = t.predicate;
                newT.object = ot.object;
                newT.confidence = std::min(t.confidence, ot.confidence);
                newT.source = "inference:transitive";

                String key = newT.subject + ":" + newT.predicate + ":" + newT.object;
                if (seen.find(key) == seen.end()) {
                    seen.insert(key);
                    inferred.push_back(newT);

                    // Record TMS justification
                    Justification j;
                    j.derivedFactId = key;
                    j.supportingFactIds = {triggerKey, ot.subject + ":" + ot.predicate + ":" + ot.object};
                    j.ruleId = "transitive:" + t.predicate;
                    j.inferenceType = "forward_chain";
                    j.confidence = newT.confidence;
                    tms_.addJustification(j);

                    // Also populate legacy justificationIndex_ for cache invalidation
                    justificationIndex_[newT.object].push_back(triggerKey);
                }
            }

            // Also find subjects
            auto subjTriples = storage_->findByPO(t.predicate, t.subject);
            for (const auto& st : subjTriples) {
                Triple newT;
                newT.subject = st.subject;
                newT.predicate = t.predicate;
                newT.object = t.object;
                newT.confidence = std::min(t.confidence, st.confidence);
                newT.source = "inference:transitive";

                String key = newT.subject + ":" + newT.predicate + ":" + newT.object;
                if (seen.find(key) == seen.end()) {
                    seen.insert(key);
                    inferred.push_back(newT);

                    // Record TMS justification
                    Justification j;
                    j.derivedFactId = key;
                    j.supportingFactIds = {triggerKey, st.subject + ":" + st.predicate + ":" + st.object};
                    j.ruleId = "transitive:" + t.predicate;
                    j.inferenceType = "forward_chain";
                    j.confidence = newT.confidence;
                    tms_.addJustification(j);

                    justificationIndex_[newT.object].push_back(triggerKey);
                }
            }
        }

        // Apply inverse relations
        if (rel && !rel->inverseProperty.empty()) {
            Triple newT;
            newT.subject = t.object;
            newT.predicate = rel->inverseProperty;
            newT.object = t.subject;
            newT.confidence = t.confidence;
            newT.source = "inference:inverse";

            String key = newT.subject + ":" + newT.predicate + ":" + newT.object;
            if (seen.find(key) == seen.end()) {
                seen.insert(key);
                inferred.push_back(newT);

                // Record TMS justification
                Justification j;
                j.derivedFactId = key;
                j.supportingFactIds = {triggerKey};
                j.ruleId = "inverse:" + t.predicate + ":" + rel->inverseProperty;
                j.inferenceType = "forward_chain";
                j.confidence = newT.confidence;
                tms_.addJustification(j);

                justificationIndex_[newT.object].push_back(triggerKey);
            }
        }

        // Apply symmetric relations
        if (rel && rel->isSymmetric) {
            Triple newT;
            newT.subject = t.object;
            newT.predicate = t.predicate;
            newT.object = t.subject;
            newT.confidence = t.confidence;
            newT.source = "inference:symmetric";

            String key = newT.subject + ":" + newT.predicate + ":" + newT.object;
            if (seen.find(key) == seen.end()) {
                seen.insert(key);
                inferred.push_back(newT);

                // Record TMS justification
                Justification j;
                j.derivedFactId = key;
                j.supportingFactIds = {triggerKey};
                j.ruleId = "symmetric:" + t.predicate;
                j.inferenceType = "forward_chain";
                j.confidence = newT.confidence;
                tms_.addJustification(j);

                justificationIndex_[newT.object].push_back(triggerKey);
            }
        }

        // Type propagation: if X rdf:type C and C subClassOf D, then X rdf:type D
        if (t.predicate == "rdf:type") {
            auto cls = storage_->getClass(t.object);
            while (cls) {
                for (const auto& superCls : cls->superClasses) {
                    Triple newT;
                    newT.subject = t.subject;
                    newT.predicate = "rdf:type";
                    newT.object = superCls;
                    newT.confidence = t.confidence * 0.95f;
                    newT.source = "inference:subClassOf";

                    String key = newT.subject + ":" + newT.predicate + ":" + newT.object;
                    if (seen.find(key) == seen.end()) {
                        seen.insert(key);
                        inferred.push_back(newT);

                        // Record TMS justification
                        Justification j;
                        j.derivedFactId = key;
                        j.supportingFactIds = {triggerKey};
                        j.ruleId = "subClassOf:" + t.object + ":" + superCls;
                        j.inferenceType = "forward_chain";
                        j.confidence = newT.confidence;
                        tms_.addJustification(j);

                        justificationIndex_[newT.object].push_back(triggerKey);
                    }
                }
                if (!cls->superClasses.empty()) {
                    cls = storage_->getClass(cls->superClasses[0]);
                } else {
                    break;
                }
            }
        }
    }

    return inferred;
}

std::vector<Triple> IncrementalReasoner::retract(
    const std::vector<Triple>& removed
) {
    std::vector<Triple> toRetract;

    // Build a fact registry from current storage for TMS lookup
    std::unordered_map<String, Triple> factRegistry;
    for (const auto& t : storage_->getAllTriples()) {
        String key = t.subject + ":" + t.predicate + ":" + t.object;
        factRegistry[key] = t;
    }

    // Use TMS-based retraction: for each removed fact, cascade to dependents
    for (const auto& t : removed) {
        auto retracted = tms_.retractWithDependents(t, factRegistry);
        for (const auto& rt : retracted) {
            String key = rt.subject + ":" + rt.predicate + ":" + rt.object;
            // Don't double-retract facts already in the removed list
            bool alreadyRemoved = false;
            for (const auto& orig : removed) {
                if (orig.subject == rt.subject && orig.predicate == rt.predicate && orig.object == rt.object) {
                    alreadyRemoved = true;
                    break;
                }
            }
            if (!alreadyRemoved) {
                toRetract.push_back(rt);
            }

            // Invalidate cache entries for retracted facts
            invalidateCache(rt.subject, rt.predicate);
        }

        // Also invalidate cache for the original removed fact
        invalidateCache(t.subject, t.predicate);
    }

    return toRetract;
}

Json IncrementalReasoner::getCacheStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Json j;
    j["cacheSize"] = static_cast<int>(inferenceCache_.size());
    j["justificationEntries"] = static_cast<int>(justificationIndex_.size());
    return j;
}

void IncrementalReasoner::clearCache() {
    std::lock_guard<std::mutex> lock(mutex_);
    inferenceCache_.clear();
    justificationIndex_.clear();
    tms_.clear();
}

void IncrementalReasoner::updateCache(
    const String& subject, const String& predicate,
    const std::vector<std::pair<String, float>>& values
) {
    String key = subject + ":" + predicate;
    inferenceCache_[key] = values;
}

std::optional<std::vector<std::pair<String, float>>>
IncrementalReasoner::getCached(
    const String& subject, const String& predicate
) const {
    String key = subject + ":" + predicate;
    auto it = inferenceCache_.find(key);
    if (it != inferenceCache_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void IncrementalReasoner::invalidateCache(
    const String& subject, const String& predicate
) {
    String key = subject + ":" + predicate;
    inferenceCache_.erase(key);
}

} // namespace ontology
