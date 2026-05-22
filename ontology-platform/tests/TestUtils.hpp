#pragma once

#include <ontology/Core.hpp>
#include <ontology/Storage.hpp>
#include <iostream>
#include <cassert>
#include <cmath>

using namespace ontology;

extern int testsPassed;
extern int testsFailed;

#define TEST(name) \
    std::cout << "Testing: " << name << "... " << std::flush;

#define PASS() \
    std::cout << "PASS" << std::endl; \
    testsPassed++;

#define FAIL(msg) \
    std::cout << "FAIL: " << msg << std::endl; \
    testsFailed++;

#define ASSERT_EQ(a, b) \
    if ((a) != (b)) { FAIL(#a " != " #b); return; }

#define ASSERT_TRUE(cond) \
    if (!(cond)) { FAIL(#cond " is false"); return; }

#define ASSERT_NEAR(a, b, eps) \
    if (std::abs((a) - (b)) > (eps)) { FAIL(#a " != " #b " (within epsilon)"); return; }

inline Triple makeTriple(const String& s, const String& p, const String& o, float conf = 1.0f) {
    Triple t;
    t.subject = s;
    t.predicate = p;
    t.object = o;
    t.confidence = conf;
    return t;
}
