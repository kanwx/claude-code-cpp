#include "TestUtils.hpp"
#include <ontology/ShaclValidation.hpp>

using namespace ontology;

int testsPassed = 0;
int testsFailed = 0;

// Helper: create an in-memory HybridStorage
static std::shared_ptr<HybridStorage> makeStorage() {
    return std::make_shared<HybridStorage>(nullptr, nullptr);
}

// Helper: add a Class with superClasses to storage
static void addClass(HybridStorage& storage, const String& id, const String& name,
                     const std::vector<String>& supers = {}) {
    Class cls;
    cls.id = id;
    cls.name = name;
    cls.superClasses = supers;
    storage.addClass(cls);
}

// Helper: add an Individual to storage
static void addIndividual(HybridStorage& storage, const String& id, const String& name,
                          const String& classId) {
    Individual ind;
    ind.id = id;
    ind.name = name;
    ind.classId = classId;
    storage.addIndividual(ind);
}

// ============================================================================
// Test 1: minCount violation
// ============================================================================
void test_minCount_violation() {
    TEST("SHACL minCount violation");

    auto storage = makeStorage();
    addClass(*storage, "Person", "Person");
    addIndividual(*storage, "alice", "Alice", "Person");
    // No "email" triples for alice

    ShaclNodeShape shape;
    shape.id = "PersonShape";
    shape.targetClass = "Person";
    ShaclPropertyShape prop;
    prop.id = "personEmail";
    prop.path = "email";
    prop.minCount = 1;
    shape.properties.push_back(prop);

    ShaclValidator validator(storage);
    validator.addShape(shape);
    auto report = validator.validateNode("alice");

    ASSERT_TRUE(!report.conforms);
    ASSERT_TRUE(report.violationCount() >= 1);

    PASS();
}

// ============================================================================
// Test 2: minCount pass
// ============================================================================
void test_minCount_pass() {
    TEST("SHACL minCount pass");

    auto storage = makeStorage();
    addClass(*storage, "Person", "Person");
    addIndividual(*storage, "alice", "Alice", "Person");
    storage->addTriple(makeTriple("alice", "manages", "bob"));

    ShaclNodeShape shape;
    shape.id = "PersonShape";
    shape.targetClass = "Person";
    ShaclPropertyShape prop;
    prop.id = "personManages";
    prop.path = "manages";
    prop.minCount = 1;
    shape.properties.push_back(prop);

    ShaclValidator validator(storage);
    validator.addShape(shape);
    auto report = validator.validateNode("alice");

    ASSERT_TRUE(report.conforms);

    PASS();
}

// ============================================================================
// Test 3: maxCount violation
// ============================================================================
void test_maxCount_violation() {
    TEST("SHACL maxCount violation");

    auto storage = makeStorage();
    addClass(*storage, "Person", "Person");
    addIndividual(*storage, "alice", "Alice", "Person");
    // Two values for "manages"
    storage->addTriple(makeTriple("alice", "manages", "bob"));
    storage->addTriple(makeTriple("alice", "manages", "carol"));

    ShaclNodeShape shape;
    shape.id = "PersonShape";
    shape.targetClass = "Person";
    ShaclPropertyShape prop;
    prop.id = "personManages";
    prop.path = "manages";
    prop.maxCount = 1;
    shape.properties.push_back(prop);

    ShaclValidator validator(storage);
    validator.addShape(shape);
    auto report = validator.validateNode("alice");

    ASSERT_TRUE(!report.conforms);
    ASSERT_TRUE(report.violationCount() >= 1);

    PASS();
}

// ============================================================================
// Test 4: sh:class violation
// ============================================================================
void test_shaclass_violation() {
    TEST("SHACL sh:class violation");

    auto storage = makeStorage();
    addClass(*storage, "Person", "Person");
    addClass(*storage, "Manager", "Manager");
    addIndividual(*storage, "alice", "Alice", "Person");
    addIndividual(*storage, "bob", "Bob", "Person");  // bob is Person, not Manager
    storage->addTriple(makeTriple("alice", "manages", "bob"));

    ShaclNodeShape shape;
    shape.id = "PersonShape";
    shape.targetClass = "Person";
    ShaclPropertyShape prop;
    prop.id = "personManages";
    prop.path = "manages";
    prop.classId = "Manager";
    shape.properties.push_back(prop);

    ShaclValidator validator(storage);
    validator.addShape(shape);
    auto report = validator.validateNode("alice");

    ASSERT_TRUE(!report.conforms);
    ASSERT_TRUE(report.violationCount() >= 1);

    PASS();
}

// ============================================================================
// Test 5: sh:class pass with subclass
// ============================================================================
void test_shaclass_pass_subclass() {
    TEST("SHACL sh:class pass with subclass");

    auto storage = makeStorage();
    addClass(*storage, "Person", "Person");
    addClass(*storage, "Employee", "Employee", {"Person"}); // Employee subclass of Person
    addIndividual(*storage, "alice", "Alice", "Person");
    addIndividual(*storage, "bob", "Bob", "Employee");      // bob is Employee, subclass of Person
    storage->addTriple(makeTriple("alice", "manages", "bob"));

    ShaclNodeShape shape;
    shape.id = "PersonShape";
    shape.targetClass = "Person";
    ShaclPropertyShape prop;
    prop.id = "personManages";
    prop.path = "manages";
    prop.classId = "Person";
    shape.properties.push_back(prop);

    ShaclValidator validator(storage);
    validator.addShape(shape);
    auto report = validator.validateNode("alice");

    ASSERT_TRUE(report.conforms);

    PASS();
}

// ============================================================================
// Test 6: sh:pattern violation
// ============================================================================
void test_pattern_violation() {
    TEST("SHACL sh:pattern violation");

    auto storage = makeStorage();
    addClass(*storage, "Person", "Person");
    addIndividual(*storage, "alice", "Alice", "Person");
    storage->addTriple(makeTriple("alice", "email", "not-an-email"));

    ShaclNodeShape shape;
    shape.id = "PersonShape";
    shape.targetClass = "Person";
    ShaclPropertyShape prop;
    prop.id = "personEmail";
    prop.path = "email";
    prop.minCount = 1;
    prop.pattern = "^[^@]+@[^@]+$";
    shape.properties.push_back(prop);

    ShaclValidator validator(storage);
    validator.addShape(shape);
    auto report = validator.validateNode("alice");

    ASSERT_TRUE(!report.conforms);
    ASSERT_TRUE(report.violationCount() >= 1);

    PASS();
}

// ============================================================================
// Test 7: sh:pattern pass
// ============================================================================
void test_pattern_pass() {
    TEST("SHACL sh:pattern pass");

    auto storage = makeStorage();
    addClass(*storage, "Person", "Person");
    addIndividual(*storage, "alice", "Alice", "Person");
    storage->addTriple(makeTriple("alice", "email", "alice@example.com"));

    ShaclNodeShape shape;
    shape.id = "PersonShape";
    shape.targetClass = "Person";
    ShaclPropertyShape prop;
    prop.id = "personEmail";
    prop.path = "email";
    prop.minCount = 1;
    prop.pattern = "^[^@]+@[^@]+$";
    shape.properties.push_back(prop);

    ShaclValidator validator(storage);
    validator.addShape(shape);
    auto report = validator.validateNode("alice");

    ASSERT_TRUE(report.conforms);

    PASS();
}

// ============================================================================
// Test 8: sh:in violation
// ============================================================================
void test_in_violation() {
    TEST("SHACL sh:in violation");

    auto storage = makeStorage();
    addClass(*storage, "Person", "Person");
    addIndividual(*storage, "alice", "Alice", "Person");
    storage->addTriple(makeTriple("alice", "status", "unknown"));

    ShaclNodeShape shape;
    shape.id = "PersonShape";
    shape.targetClass = "Person";
    ShaclPropertyShape prop;
    prop.id = "personStatus";
    prop.path = "status";
    prop.minCount = 1;
    prop.inValues = {"active", "inactive", "suspended"};
    shape.properties.push_back(prop);

    ShaclValidator validator(storage);
    validator.addShape(shape);
    auto report = validator.validateNode("alice");

    ASSERT_TRUE(!report.conforms);
    ASSERT_TRUE(report.violationCount() >= 1);

    PASS();
}

// ============================================================================
// Test 9: sh:in pass
// ============================================================================
void test_in_pass() {
    TEST("SHACL sh:in pass");

    auto storage = makeStorage();
    addClass(*storage, "Person", "Person");
    addIndividual(*storage, "alice", "Alice", "Person");
    storage->addTriple(makeTriple("alice", "status", "active"));

    ShaclNodeShape shape;
    shape.id = "PersonShape";
    shape.targetClass = "Person";
    ShaclPropertyShape prop;
    prop.id = "personStatus";
    prop.path = "status";
    prop.minCount = 1;
    prop.inValues = {"active", "inactive", "suspended"};
    shape.properties.push_back(prop);

    ShaclValidator validator(storage);
    validator.addShape(shape);
    auto report = validator.validateNode("alice");

    ASSERT_TRUE(report.conforms);

    PASS();
}

// ============================================================================
// Test 10: sh:minValue / sh:maxValue
// ============================================================================
void test_min_max_value() {
    TEST("SHACL sh:minValue/sh:maxValue");

    auto storage = makeStorage();
    addClass(*storage, "Person", "Person");

    // Violation: value too low
    {
        addIndividual(*storage, "alice", "Alice", "Person");
        storage->addTriple(makeTriple("alice", "age", "15"));

        ShaclNodeShape shape;
        shape.id = "PersonShape";
        shape.targetClass = "Person";
        ShaclPropertyShape prop;
        prop.id = "personAge";
        prop.path = "age";
        prop.minCount = 1;
        prop.minValue = 18.0;
        prop.maxValue = 120.0;
        shape.properties.push_back(prop);

        ShaclValidator validator(storage);
        validator.addShape(shape);
        auto report = validator.validateNode("alice");

        ASSERT_TRUE(!report.conforms);
        ASSERT_TRUE(report.violationCount() >= 1);
    }

    // Violation: value too high
    {
        addIndividual(*storage, "bob", "Bob", "Person");
        storage->addTriple(makeTriple("bob", "age", "200"));

        ShaclNodeShape shape;
        shape.id = "PersonShape2";
        shape.targetClass = "Person";
        ShaclPropertyShape prop;
        prop.id = "personAge2";
        prop.path = "age";
        prop.minCount = 1;
        prop.minValue = 18.0;
        prop.maxValue = 120.0;
        shape.properties.push_back(prop);

        ShaclValidator validator(storage);
        validator.addShape(shape);
        auto report = validator.validateNode("bob");

        ASSERT_TRUE(!report.conforms);
        ASSERT_TRUE(report.violationCount() >= 1);
    }

    // Pass: value in range
    {
        addIndividual(*storage, "carol", "Carol", "Person");
        storage->addTriple(makeTriple("carol", "age", "30"));

        ShaclNodeShape shape;
        shape.id = "PersonShape3";
        shape.targetClass = "Person";
        ShaclPropertyShape prop;
        prop.id = "personAge3";
        prop.path = "age";
        prop.minCount = 1;
        prop.minValue = 18.0;
        prop.maxValue = 120.0;
        shape.properties.push_back(prop);

        ShaclValidator validator(storage);
        validator.addShape(shape);
        auto report = validator.validateNode("carol");

        ASSERT_TRUE(report.conforms);
    }

    PASS();
}

// ============================================================================
// Test 11: sh:exclusiveMinValue / sh:exclusiveMaxValue
// ============================================================================
void test_exclusive_min_max_value() {
    TEST("SHACL sh:exclusiveMinValue/sh:exclusiveMaxValue");

    // Violation: value exactly at exclusive boundary
    // NOTE: Implementation nests exclusive checks inside the
    // if(minValue.has_value() || maxValue.has_value()) guard,
    // so we must also set minValue/maxValue for exclusive to be checked.
    {
        auto storage = makeStorage();
        addClass(*storage, "Person", "Person");
        addIndividual(*storage, "alice", "Alice", "Person");
        storage->addTriple(makeTriple("alice", "score", "10"));

        ShaclNodeShape shape;
        shape.id = "PersonShape";
        shape.targetClass = "Person";
        ShaclPropertyShape prop;
        prop.id = "personScore";
        prop.path = "score";
        prop.minCount = 1;
        prop.minValue = 0.0;        // needed to activate the numeric check guard
        prop.exclusiveMinValue = 10.0;  // 10 is NOT > 10 -> violation
        prop.maxValue = 200.0;      // needed to activate the guard
        prop.exclusiveMaxValue = 100.0; // 10 < 100 -> passes this check
        shape.properties.push_back(prop);

        ShaclValidator validator(storage);
        validator.addShape(shape);
        auto report = validator.validateNode("alice");

        // 10 <= 10 (exclusive min) -> violation
        ASSERT_TRUE(!report.conforms);
        ASSERT_TRUE(report.violationCount() >= 1);
    }

    // Pass: value strictly within exclusive bounds
    {
        auto storage = makeStorage();
        addClass(*storage, "Person", "Person");
        addIndividual(*storage, "bob", "Bob", "Person");
        storage->addTriple(makeTriple("bob", "score", "11"));

        ShaclNodeShape shape;
        shape.id = "PersonShape2";
        shape.targetClass = "Person";
        ShaclPropertyShape prop;
        prop.id = "personScore2";
        prop.path = "score";
        prop.minCount = 1;
        prop.minValue = 0.0;
        prop.exclusiveMinValue = 10.0;  // 11 > 10 -> passes
        prop.maxValue = 200.0;
        prop.exclusiveMaxValue = 100.0; // 11 < 100 -> passes
        shape.properties.push_back(prop);

        ShaclValidator validator(storage);
        validator.addShape(shape);
        auto report = validator.validateNode("bob");

        ASSERT_TRUE(report.conforms);
    }

    // Violation: value at exclusive max boundary (100 is NOT < 100)
    {
        auto storage = makeStorage();
        addClass(*storage, "Person", "Person");
        addIndividual(*storage, "carol", "Carol", "Person");
        storage->addTriple(makeTriple("carol", "score", "100"));

        ShaclNodeShape shape;
        shape.id = "PersonShape3";
        shape.targetClass = "Person";
        ShaclPropertyShape prop;
        prop.id = "personScore3";
        prop.path = "score";
        prop.minCount = 1;
        prop.minValue = 0.0;
        prop.exclusiveMinValue = 10.0;
        prop.maxValue = 200.0;
        prop.exclusiveMaxValue = 100.0;  // 100 >= 100 -> violation
        shape.properties.push_back(prop);

        ShaclValidator validator(storage);
        validator.addShape(shape);
        auto report = validator.validateNode("carol");

        ASSERT_TRUE(!report.conforms);
    }

    PASS();
}

// ============================================================================
// Test 12: sh:hasValue violation
// ============================================================================
void test_hasValue_violation() {
    TEST("SHACL sh:hasValue violation");

    auto storage = makeStorage();
    addClass(*storage, "Person", "Person");
    addIndividual(*storage, "alice", "Alice", "Person");
    storage->addTriple(makeTriple("alice", "country", "Canada"));

    ShaclNodeShape shape;
    shape.id = "PersonShape";
    shape.targetClass = "Person";
    ShaclPropertyShape prop;
    prop.id = "personCountry";
    prop.path = "country";
    prop.minCount = 1;
    prop.hasValue = true;
    prop.fixedValue = "USA";
    shape.properties.push_back(prop);

    ShaclValidator validator(storage);
    validator.addShape(shape);
    auto report = validator.validateNode("alice");

    ASSERT_TRUE(!report.conforms);
    ASSERT_TRUE(report.violationCount() >= 1);

    PASS();
}

// ============================================================================
// Test 13: sh:hasValue pass
// ============================================================================
void test_hasValue_pass() {
    TEST("SHACL sh:hasValue pass");

    auto storage = makeStorage();
    addClass(*storage, "Person", "Person");
    addIndividual(*storage, "alice", "Alice", "Person");
    storage->addTriple(makeTriple("alice", "country", "USA"));

    ShaclNodeShape shape;
    shape.id = "PersonShape";
    shape.targetClass = "Person";
    ShaclPropertyShape prop;
    prop.id = "personCountry";
    prop.path = "country";
    prop.minCount = 1;
    prop.hasValue = true;
    prop.fixedValue = "USA";
    shape.properties.push_back(prop);

    ShaclValidator validator(storage);
    validator.addShape(shape);
    auto report = validator.validateNode("alice");

    ASSERT_TRUE(report.conforms);

    PASS();
}

// ============================================================================
// Test 14: sh:datatype violation
// ============================================================================
void test_datatype_violation() {
    TEST("SHACL sh:datatype violation");

    auto storage = makeStorage();
    addClass(*storage, "Person", "Person");
    addIndividual(*storage, "alice", "Alice", "Person");
    storage->addTriple(makeTriple("alice", "age", "abc"));  // not an integer

    ShaclNodeShape shape;
    shape.id = "PersonShape";
    shape.targetClass = "Person";
    ShaclPropertyShape prop;
    prop.id = "personAge";
    prop.path = "age";
    prop.minCount = 1;
    prop.datatype = "http://www.w3.org/2001/XMLSchema#integer";
    shape.properties.push_back(prop);

    ShaclValidator validator(storage);
    validator.addShape(shape);
    auto report = validator.validateNode("alice");

    ASSERT_TRUE(!report.conforms);
    ASSERT_TRUE(report.violationCount() >= 1);

    PASS();
}

// ============================================================================
// Test 15: sh:datatype pass
// ============================================================================
void test_datatype_pass() {
    TEST("SHACL sh:datatype pass");

    auto storage = makeStorage();
    addClass(*storage, "Person", "Person");
    addIndividual(*storage, "alice", "Alice", "Person");
    storage->addTriple(makeTriple("alice", "age", "42"));

    ShaclNodeShape shape;
    shape.id = "PersonShape";
    shape.targetClass = "Person";
    ShaclPropertyShape prop;
    prop.id = "personAge";
    prop.path = "age";
    prop.minCount = 1;
    prop.datatype = "http://www.w3.org/2001/XMLSchema#integer";
    shape.properties.push_back(prop);

    ShaclValidator validator(storage);
    validator.addShape(shape);
    auto report = validator.validateNode("alice");

    ASSERT_TRUE(report.conforms);

    PASS();
}

// ============================================================================
// Test 16: sh:closed violation
// ============================================================================
void test_closed_violation() {
    TEST("SHACL sh:closed violation");

    auto storage = makeStorage();
    addClass(*storage, "Person", "Person");
    addIndividual(*storage, "alice", "Alice", "Person");
    // Allowed: name, email.  Not allowed: phone
    storage->addTriple(makeTriple("alice", "name", "Alice"));
    storage->addTriple(makeTriple("alice", "email", "alice@example.com"));
    storage->addTriple(makeTriple("alice", "phone", "555-1234"));  // not allowed

    ShaclNodeShape shape;
    shape.id = "PersonShape";
    shape.targetClass = "Person";
    shape.closed = true;
    // Only name and email are allowed; phone is not
    ShaclPropertyShape p1;
    p1.id = "personName";
    p1.path = "name";
    p1.minCount = 1;
    ShaclPropertyShape p2;
    p2.id = "personEmail";
    p2.path = "email";
    p2.minCount = 1;
    shape.properties.push_back(p1);
    shape.properties.push_back(p2);

    ShaclValidator validator(storage);
    validator.addShape(shape);
    auto report = validator.validateNode("alice");

    ASSERT_TRUE(!report.conforms);
    ASSERT_TRUE(report.violationCount() >= 1);

    PASS();
}

// ============================================================================
// Test 17: sh:or constraint
// ============================================================================
void test_or_constraint() {
    TEST("SHACL sh:or constraint");

    // Pass case: alice has "name", so SubShape1 passes -> or passes
    {
        auto storage = makeStorage();
        addClass(*storage, "Person", "Person");
        addIndividual(*storage, "alice", "Alice", "Person");
        storage->addTriple(makeTriple("alice", "name", "Alice"));
        // No "age" triple

        // Sub-shapes (no targetClass needed; logical constraints validate props directly)
        ShaclNodeShape sub1;
        sub1.id = "SubShape1";
        ShaclPropertyShape p1;
        p1.id = "p1";
        p1.path = "name";
        p1.minCount = 1;
        sub1.properties.push_back(p1);

        ShaclNodeShape sub2;
        sub2.id = "SubShape2";
        ShaclPropertyShape p2;
        p2.id = "p2";
        p2.path = "age";
        p2.minCount = 1;
        sub2.properties.push_back(p2);

        // Main shape with sh:or
        ShaclNodeShape orShape;
        orShape.id = "OrShape";
        orShape.targetClass = "Person";
        orShape.orShapes = {"SubShape1", "SubShape2"};

        ShaclValidator validator(storage);
        validator.addShape(sub1);
        validator.addShape(sub2);
        validator.addShape(orShape);
        auto report = validator.validateNode("alice");

        ASSERT_TRUE(report.conforms);
    }

    // Fail case: bob has neither "name" nor "age"
    {
        auto storage = makeStorage();
        addClass(*storage, "Person", "Person");
        addIndividual(*storage, "bob", "Bob", "Person");
        // No "name" or "age" triples

        ShaclNodeShape sub1;
        sub1.id = "SubShape1";
        ShaclPropertyShape p1;
        p1.id = "p1";
        p1.path = "name";
        p1.minCount = 1;
        sub1.properties.push_back(p1);

        ShaclNodeShape sub2;
        sub2.id = "SubShape2";
        ShaclPropertyShape p2;
        p2.id = "p2";
        p2.path = "age";
        p2.minCount = 1;
        sub2.properties.push_back(p2);

        ShaclNodeShape orShape;
        orShape.id = "OrShape";
        orShape.targetClass = "Person";
        orShape.orShapes = {"SubShape1", "SubShape2"};

        ShaclValidator validator(storage);
        validator.addShape(sub1);
        validator.addShape(sub2);
        validator.addShape(orShape);
        auto report = validator.validateNode("bob");

        ASSERT_TRUE(!report.conforms);
    }

    PASS();
}

// ============================================================================
// Test 18: sh:and constraint
// ============================================================================
void test_and_constraint() {
    TEST("SHACL sh:and constraint");

    // Pass case: alice has both "name" and "email"
    {
        auto storage = makeStorage();
        addClass(*storage, "Person", "Person");
        addIndividual(*storage, "alice", "Alice", "Person");
        storage->addTriple(makeTriple("alice", "name", "Alice"));
        storage->addTriple(makeTriple("alice", "email", "alice@example.com"));

        ShaclNodeShape sub1;
        sub1.id = "SubShape1";
        ShaclPropertyShape p1;
        p1.id = "p1";
        p1.path = "name";
        p1.minCount = 1;
        sub1.properties.push_back(p1);

        ShaclNodeShape sub2;
        sub2.id = "SubShape2";
        ShaclPropertyShape p2;
        p2.id = "p2";
        p2.path = "email";
        p2.minCount = 1;
        sub2.properties.push_back(p2);

        ShaclNodeShape andShape;
        andShape.id = "AndShape";
        andShape.targetClass = "Person";
        andShape.andShapes = {"SubShape1", "SubShape2"};

        ShaclValidator validator(storage);
        validator.addShape(sub1);
        validator.addShape(sub2);
        validator.addShape(andShape);
        auto report = validator.validateNode("alice");

        ASSERT_TRUE(report.conforms);
    }

    // Fail case: bob has "name" but no "email"
    {
        auto storage = makeStorage();
        addClass(*storage, "Person", "Person");
        addIndividual(*storage, "bob", "Bob", "Person");
        storage->addTriple(makeTriple("bob", "name", "Bob"));
        // No "email"

        ShaclNodeShape sub1;
        sub1.id = "SubShape1";
        ShaclPropertyShape p1;
        p1.id = "p1";
        p1.path = "name";
        p1.minCount = 1;
        sub1.properties.push_back(p1);

        ShaclNodeShape sub2;
        sub2.id = "SubShape2";
        ShaclPropertyShape p2;
        p2.id = "p2";
        p2.path = "email";
        p2.minCount = 1;
        sub2.properties.push_back(p2);

        ShaclNodeShape andShape;
        andShape.id = "AndShape";
        andShape.targetClass = "Person";
        andShape.andShapes = {"SubShape1", "SubShape2"};

        ShaclValidator validator(storage);
        validator.addShape(sub1);
        validator.addShape(sub2);
        validator.addShape(andShape);
        auto report = validator.validateNode("bob");

        ASSERT_TRUE(!report.conforms);
    }

    PASS();
}

// ============================================================================
// Test 19: sh:not constraint
// ============================================================================
void test_not_constraint() {
    TEST("SHACL sh:not constraint");

    // Fail case: alice conforms to the negated shape (has "secret"), so NOT fails
    {
        auto storage = makeStorage();
        addClass(*storage, "Person", "Person");
        addIndividual(*storage, "alice", "Alice", "Person");
        storage->addTriple(makeTriple("alice", "secret", "password123"));

        ShaclNodeShape negatedShape;
        negatedShape.id = "HasSecretShape";
        ShaclPropertyShape p1;
        p1.id = "p1";
        p1.path = "secret";
        p1.minCount = 1;
        negatedShape.properties.push_back(p1);

        ShaclNodeShape notShape;
        notShape.id = "NotSecretShape";
        notShape.targetClass = "Person";
        notShape.notShape = "HasSecretShape";

        ShaclValidator validator(storage);
        validator.addShape(negatedShape);
        validator.addShape(notShape);
        auto report = validator.validateNode("alice");

        ASSERT_TRUE(!report.conforms);
    }

    // Pass case: bob does NOT have "secret", so negated shape fails -> NOT passes
    {
        auto storage = makeStorage();
        addClass(*storage, "Person", "Person");
        addIndividual(*storage, "bob", "Bob", "Person");
        // No "secret" triple

        ShaclNodeShape negatedShape;
        negatedShape.id = "HasSecretShape";
        ShaclPropertyShape p1;
        p1.id = "p1";
        p1.path = "secret";
        p1.minCount = 1;
        negatedShape.properties.push_back(p1);

        ShaclNodeShape notShape;
        notShape.id = "NotSecretShape";
        notShape.targetClass = "Person";
        notShape.notShape = "HasSecretShape";

        ShaclValidator validator(storage);
        validator.addShape(negatedShape);
        validator.addShape(notShape);
        auto report = validator.validateNode("bob");

        ASSERT_TRUE(report.conforms);
    }

    PASS();
}

// ============================================================================
// Test 20: sh:xone constraint
// ============================================================================
void test_xone_constraint() {
    TEST("SHACL sh:xone constraint");

    // Pass case: alice has "name" but not "age" -> exactly 1 sub-shape passes
    {
        auto storage = makeStorage();
        addClass(*storage, "Person", "Person");
        addIndividual(*storage, "alice", "Alice", "Person");
        storage->addTriple(makeTriple("alice", "name", "Alice"));

        ShaclNodeShape sub1;
        sub1.id = "SubShape1";
        ShaclPropertyShape p1;
        p1.id = "p1";
        p1.path = "name";
        p1.minCount = 1;
        sub1.properties.push_back(p1);

        ShaclNodeShape sub2;
        sub2.id = "SubShape2";
        ShaclPropertyShape p2;
        p2.id = "p2";
        p2.path = "age";
        p2.minCount = 1;
        sub2.properties.push_back(p2);

        ShaclNodeShape xoneShape;
        xoneShape.id = "XoneShape";
        xoneShape.targetClass = "Person";
        xoneShape.xoneShape = "SubShape1,SubShape2";  // comma-separated

        ShaclValidator validator(storage);
        validator.addShape(sub1);
        validator.addShape(sub2);
        validator.addShape(xoneShape);
        auto report = validator.validateNode("alice");

        ASSERT_TRUE(report.conforms);
    }

    // Fail case: bob has both "name" and "age" -> 2 sub-shapes pass
    {
        auto storage = makeStorage();
        addClass(*storage, "Person", "Person");
        addIndividual(*storage, "bob", "Bob", "Person");
        storage->addTriple(makeTriple("bob", "name", "Bob"));
        storage->addTriple(makeTriple("bob", "age", "30"));

        ShaclNodeShape sub1;
        sub1.id = "SubShape1";
        ShaclPropertyShape p1;
        p1.id = "p1";
        p1.path = "name";
        p1.minCount = 1;
        sub1.properties.push_back(p1);

        ShaclNodeShape sub2;
        sub2.id = "SubShape2";
        ShaclPropertyShape p2;
        p2.id = "p2";
        p2.path = "age";
        p2.minCount = 1;
        sub2.properties.push_back(p2);

        ShaclNodeShape xoneShape;
        xoneShape.id = "XoneShape";
        xoneShape.targetClass = "Person";
        xoneShape.xoneShape = "SubShape1,SubShape2";

        ShaclValidator validator(storage);
        validator.addShape(sub1);
        validator.addShape(sub2);
        validator.addShape(xoneShape);
        auto report = validator.validateNode("bob");

        ASSERT_TRUE(!report.conforms);
    }

    PASS();
}

// ============================================================================
// Test 21: Deactivated shape
// ============================================================================
void test_deactivated_shape() {
    TEST("SHACL deactivated shape");

    auto storage = makeStorage();
    addClass(*storage, "Person", "Person");
    addIndividual(*storage, "alice", "Alice", "Person");
    // No "email" triple, but shape is deactivated

    ShaclNodeShape shape;
    shape.id = "PersonShape";
    shape.targetClass = "Person";
    shape.deactivated = true;
    ShaclPropertyShape prop;
    prop.id = "personEmail";
    prop.path = "email";
    prop.minCount = 1;
    shape.properties.push_back(prop);

    ShaclValidator validator(storage);
    validator.addShape(shape);
    auto report = validator.validateNode("alice");

    // Deactivated shape should be skipped -> conforms
    ASSERT_TRUE(report.conforms);

    PASS();
}

// ============================================================================
// Test 22: validateNode for non-existent individual
// ============================================================================
void test_validate_nonexistent() {
    TEST("SHACL validateNode for non-existent individual");

    auto storage = makeStorage();

    ShaclNodeShape shape;
    shape.id = "PersonShape";
    shape.targetClass = "Person";
    ShaclPropertyShape prop;
    prop.id = "personEmail";
    prop.path = "email";
    prop.minCount = 1;
    shape.properties.push_back(prop);

    ShaclValidator validator(storage);
    validator.addShape(shape);
    auto report = validator.validateNode("nobody");

    // Non-existent individual -> no violations to check -> conforms
    ASSERT_TRUE(report.conforms);

    PASS();
}

// ============================================================================
// Test 23: Multiple violations per property
// ============================================================================
void test_multiple_violations_per_property() {
    TEST("SHACL multiple violations per property");

    auto storage = makeStorage();
    addClass(*storage, "Person", "Person");
    addIndividual(*storage, "alice", "Alice", "Person");
    // No "email" triples -> minCount violation
    // And pattern violation won't fire because there are no values to check.
    // Instead, use a value that violates pattern but also test minCount=2
    storage->addTriple(makeTriple("alice", "email", "bad"));  // 1 value, pattern fails

    ShaclNodeShape shape;
    shape.id = "PersonShape";
    shape.targetClass = "Person";
    ShaclPropertyShape prop;
    prop.id = "personEmail";
    prop.path = "email";
    prop.minCount = 2;  // Requires 2, only 1 -> violation
    prop.pattern = "^[^@]+@[^@]+$";  // "bad" doesn't match -> violation
    shape.properties.push_back(prop);

    ShaclValidator validator(storage);
    validator.addShape(shape);
    auto report = validator.validateNode("alice");

    ASSERT_TRUE(!report.conforms);
    // Should have at least 2 violations: minCount + pattern
    ASSERT_TRUE(report.violationCount() >= 2);

    PASS();
}

// ============================================================================
// Test 24: loadShapesFromJson
// ============================================================================
void test_loadShapesFromJson() {
    TEST("SHACL loadShapesFromJson");

    auto storage = makeStorage();

    Json shapesJson = Json::array();
    Json shape1Json;
    shape1Json["id"] = "PersonShape";
    shape1Json["targetClass"] = "Person";
    shape1Json["closed"] = false;
    shape1Json["properties"] = Json::array();
    Json prop1Json;
    prop1Json["id"] = "personEmail";
    prop1Json["path"] = "email";
    prop1Json["minCount"] = 1;
    shape1Json["properties"].push_back(prop1Json);
    shapesJson.push_back(shape1Json);

    Json shape2Json;
    shape2Json["id"] = "EmployeeShape";
    shape2Json["targetClass"] = "Employee";
    shape2Json["properties"] = Json::array();
    shapesJson.push_back(shape2Json);

    ShaclValidator validator(storage);
    validator.loadShapesFromJson(shapesJson);

    auto shapes = validator.getShapes();
    ASSERT_EQ(shapes.size(), 2u);

    // Verify one of them has the expected property
    bool foundPersonShape = false;
    for (const auto& s : shapes) {
        if (s.id == "PersonShape") {
            foundPersonShape = true;
            ASSERT_EQ(s.properties.size(), 1u);
            ASSERT_EQ(s.properties[0].path, "email");
            ASSERT_EQ(s.properties[0].minCount, 1);
        }
    }
    ASSERT_TRUE(foundPersonShape);

    PASS();
}

// ============================================================================
// Test 25: sh:targetSubjectsOf
// ============================================================================
void test_targetSubjectsOf() {
    TEST("SHACL sh:targetSubjectsOf");

    auto storage = makeStorage();
    addClass(*storage, "Person", "Person");
    addIndividual(*storage, "alice", "Alice", "Person");
    storage->addTriple(makeTriple("alice", "manages", "bob"));
    // alice has "manages" -> should be targeted

    // Shape targets subjects of "manages" and requires minCount=1 for "department"
    ShaclNodeShape shape;
    shape.id = "ManagerShape";
    shape.targetSubjectsOf = {"manages"};
    ShaclPropertyShape prop;
    prop.id = "managerDept";
    prop.path = "department";
    prop.minCount = 1;
    shape.properties.push_back(prop);

    ShaclValidator validator(storage);
    validator.addShape(shape);
    auto report = validator.validateNode("alice");

    // alice is targetSubjectsOf "manages" but has no "department" -> violation
    ASSERT_TRUE(!report.conforms);
    ASSERT_TRUE(report.violationCount() >= 1);

    PASS();
}

// ============================================================================
// Main
// ============================================================================
int main() {
    test_minCount_violation();
    test_minCount_pass();
    test_maxCount_violation();
    test_shaclass_violation();
    test_shaclass_pass_subclass();
    test_pattern_violation();
    test_pattern_pass();
    test_in_violation();
    test_in_pass();
    test_min_max_value();
    test_exclusive_min_max_value();
    test_hasValue_violation();
    test_hasValue_pass();
    test_datatype_violation();
    test_datatype_pass();
    test_closed_violation();
    test_or_constraint();
    test_and_constraint();
    test_not_constraint();
    test_xone_constraint();
    test_deactivated_shape();
    test_validate_nonexistent();
    test_multiple_violations_per_property();
    test_loadShapesFromJson();
    test_targetSubjectsOf();

    std::cout << "\nResults: " << testsPassed << " passed, " << testsFailed << " failed" << std::endl;
    return testsFailed > 0 ? 1 : 0;
}
