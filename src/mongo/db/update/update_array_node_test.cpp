/**
 * Copyright (C) 2017 MongoDB Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, the copyright holders give permission to link the
 * code of portions of this program with the OpenSSL library under certain
 * conditions as described in each individual source file and distribute
 * linked combinations including the program with the OpenSSL library. You
 * must comply with the GNU Affero General Public License in all respects
 * for all of the code used other than as permitted herein. If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version. If you
 * delete this exception statement from all source files in the program,
 * then also delete it in the license file.
 */

#include "mongo/platform/basic.h"

#include "mongo/db/update/update_array_node.h"

#include "mongo/bson/mutable/algorithm.h"
#include "mongo/bson/mutable/mutable_bson_test_utils.h"
#include "mongo/db/json.h"
#include "mongo/db/update/update_node_test_fixture.h"
#include "mongo/db/update/update_object_node.h"
#include "mongo/unittest/death_test.h"
#include "mongo/unittest/unittest.h"

namespace mongo {
namespace {

using UpdateArrayNodeTest = UpdateNodeTest;
using mongo::mutablebson::Element;

TEST_F(UpdateArrayNodeTest, ApplyCreatePathFails) {
    auto update = fromjson("{$set: {'a.b.$[i]': 0}}");
    auto arrayFilter = fromjson("{i: 0}");
    const CollatorInterface* collator = nullptr;
    std::map<StringData, std::unique_ptr<ExpressionWithPlaceholder>> arrayFilters;
    arrayFilters["i"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilter, collator));
    std::set<std::string> foundIdentifiers;
    UpdateObjectNode root;
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.b.$[i]"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));

    mutablebson::Document doc(fromjson("{a: {}}"));
    addIndexedPath("a");
    ASSERT_THROWS_CODE_AND_WHAT(
        root.apply(getApplyParams(doc.root())),
        UserException,
        ErrorCodes::BadValue,
        "The path 'a.b' must exist in the document in order to apply array updates.");
}

TEST_F(UpdateArrayNodeTest, ApplyToNonArrayFails) {
    auto update = fromjson("{$set: {'a.$[i]': 0}}");
    auto arrayFilter = fromjson("{i: 0}");
    const CollatorInterface* collator = nullptr;
    std::map<StringData, std::unique_ptr<ExpressionWithPlaceholder>> arrayFilters;
    arrayFilters["i"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilter, collator));
    std::set<std::string> foundIdentifiers;
    UpdateObjectNode root;
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[i]"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));

    mutablebson::Document doc(fromjson("{a: {}}"));
    addIndexedPath("a");
    ASSERT_THROWS_CODE_AND_WHAT(root.apply(getApplyParams(doc.root())),
                                UserException,
                                ErrorCodes::BadValue,
                                "Cannot apply array updates to non-array element a: {}");
}

TEST_F(UpdateArrayNodeTest, UpdateIsAppliedToAllMatchingElements) {
    auto update = fromjson("{$set: {'a.$[i]': 2}}");
    auto arrayFilter = fromjson("{i: 0}");
    const CollatorInterface* collator = nullptr;
    std::map<StringData, std::unique_ptr<ExpressionWithPlaceholder>> arrayFilters;
    arrayFilters["i"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilter, collator));
    std::set<std::string> foundIdentifiers;
    UpdateObjectNode root;
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[i]"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));

    mutablebson::Document doc(fromjson("{a: [0, 1, 0]}"));
    addIndexedPath("a");
    auto result = root.apply(getApplyParams(doc.root()));
    ASSERT_TRUE(result.indexesAffected);
    ASSERT_FALSE(result.noop);
    ASSERT_EQUALS(fromjson("{a: [2, 1, 2]}"), doc);
    ASSERT_TRUE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{$set: {a: [2, 1, 2]}}"), getLogDoc());
}

DEATH_TEST_F(UpdateArrayNodeTest,
             ArrayElementsMustNotBeDeserialized,
             "Invariant failure childElement.hasValue()") {
    auto update = fromjson("{$set: {'a.$[i].b': 0}}");
    auto arrayFilter = fromjson("{'i.c': 0}");
    const CollatorInterface* collator = nullptr;
    std::map<StringData, std::unique_ptr<ExpressionWithPlaceholder>> arrayFilters;
    arrayFilters["i"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilter, collator));
    std::set<std::string> foundIdentifiers;
    UpdateObjectNode root;
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[i].b"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));

    mutablebson::Document doc(fromjson("{a: [{c: 0}, {c: 0}, {c: 1}]}"));
    doc.root()["a"]["1"]["c"].setValueInt(1).transitional_ignore();
    doc.root()["a"]["2"]["c"].setValueInt(0).transitional_ignore();
    addIndexedPath("a");
    root.apply(getApplyParams(doc.root()));
}

TEST_F(UpdateArrayNodeTest, UpdateForEmptyIdentifierIsAppliedToAllArrayElements) {
    auto update = fromjson("{$set: {'a.$[]': 1}}");
    const CollatorInterface* collator = nullptr;
    std::map<StringData, std::unique_ptr<ExpressionWithPlaceholder>> arrayFilters;
    std::set<std::string> foundIdentifiers;
    UpdateObjectNode root;
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[]"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));

    mutablebson::Document doc(fromjson("{a: [0, 0, 0]}"));
    addIndexedPath("a");
    auto result = root.apply(getApplyParams(doc.root()));
    ASSERT_TRUE(result.indexesAffected);
    ASSERT_FALSE(result.noop);
    ASSERT_EQUALS(fromjson("{a: [1, 1, 1]}"), doc);
    ASSERT_TRUE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{$set: {a: [1, 1, 1]}}"), getLogDoc());
}

TEST_F(UpdateArrayNodeTest, ApplyMultipleUpdatesToArrayElement) {
    auto update = fromjson("{$set: {'a.$[i].b': 1, 'a.$[j].c': 1, 'a.$[k].d': 1}}");
    auto arrayFilterI = fromjson("{'i.b': 0}");
    auto arrayFilterJ = fromjson("{'j.c': 0}");
    auto arrayFilterK = fromjson("{'k.d': 0}");
    const CollatorInterface* collator = nullptr;
    std::map<StringData, std::unique_ptr<ExpressionWithPlaceholder>> arrayFilters;
    arrayFilters["i"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilterI, collator));
    arrayFilters["j"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilterJ, collator));
    arrayFilters["k"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilterK, collator));
    std::set<std::string> foundIdentifiers;
    UpdateObjectNode root;
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[i].b"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[j].c"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[k].d"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));

    mutablebson::Document doc(fromjson("{a: [{b: 0, c: 0, d: 0}]}"));
    addIndexedPath("a");
    auto result = root.apply(getApplyParams(doc.root()));
    ASSERT_TRUE(result.indexesAffected);
    ASSERT_FALSE(result.noop);
    ASSERT_EQUALS(fromjson("{a: [{b: 1, c: 1, d: 1}]}"), doc);
    ASSERT_TRUE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{$set: {'a.0.b': 1, 'a.0.c': 1, 'a.0.d': 1}}"), getLogDoc());
}

TEST_F(UpdateArrayNodeTest, ApplyMultipleUpdatesToArrayElementsUsingMergedChildrenCache) {
    auto update = fromjson("{$set: {'a.$[i].b': 1, 'a.$[j].c': 1}}");
    auto arrayFilterI = fromjson("{'i.b': 0}");
    auto arrayFilterJ = fromjson("{'j.c': 0}");
    const CollatorInterface* collator = nullptr;
    std::map<StringData, std::unique_ptr<ExpressionWithPlaceholder>> arrayFilters;
    arrayFilters["i"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilterI, collator));
    arrayFilters["j"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilterJ, collator));
    std::set<std::string> foundIdentifiers;
    UpdateObjectNode root;
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[i].b"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[j].c"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));

    mutablebson::Document doc(fromjson("{a: [{b: 0, c: 0}, {b: 0, c: 0}]}"));
    addIndexedPath("a");
    auto result = root.apply(getApplyParams(doc.root()));
    ASSERT_TRUE(result.indexesAffected);
    ASSERT_FALSE(result.noop);
    ASSERT_EQUALS(fromjson("{a: [{b: 1, c: 1}, {b: 1, c: 1}]}"), doc);
    ASSERT_TRUE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{$set: {a: [{b: 1, c: 1}, {b: 1, c: 1}]}}"), getLogDoc());
}

TEST_F(UpdateArrayNodeTest, ApplyMultipleUpdatesToArrayElementsWithoutMergedChildrenCache) {
    auto update = fromjson("{$set: {'a.$[i].b': 2, 'a.$[j].c': 2, 'a.$[k].d': 2}}");
    auto arrayFilterI = fromjson("{'i.b': 0}");
    auto arrayFilterJ = fromjson("{'j.c': 0}");
    auto arrayFilterK = fromjson("{'k.d': 0}");
    const CollatorInterface* collator = nullptr;
    std::map<StringData, std::unique_ptr<ExpressionWithPlaceholder>> arrayFilters;
    arrayFilters["i"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilterI, collator));
    arrayFilters["j"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilterJ, collator));
    arrayFilters["k"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilterK, collator));
    std::set<std::string> foundIdentifiers;
    UpdateObjectNode root;
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[i].b"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[j].c"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[k].d"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));

    mutablebson::Document doc(fromjson("{a: [{b: 0, c: 0, d: 1}, {b: 1, c: 0, d: 0}]}"));
    addIndexedPath("a");
    auto result = root.apply(getApplyParams(doc.root()));
    ASSERT_TRUE(result.indexesAffected);
    ASSERT_FALSE(result.noop);
    ASSERT_EQUALS(fromjson("{a: [{b: 2, c: 2, d: 1}, {b: 1, c: 2, d: 2}]}"), doc);
    ASSERT_TRUE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{$set: {a: [{b: 2, c: 2, d: 1}, {b: 1, c: 2, d: 2}]}}"), getLogDoc());
}

TEST_F(UpdateArrayNodeTest, ApplyMultipleUpdatesToArrayElementWithEmptyIdentifiers) {
    auto update = fromjson("{$set: {'a.$[].b': 1, 'a.$[].c': 1}}");
    const CollatorInterface* collator = nullptr;
    std::map<StringData, std::unique_ptr<ExpressionWithPlaceholder>> arrayFilters;
    std::set<std::string> foundIdentifiers;
    UpdateObjectNode root;
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[].b"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[].c"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));

    mutablebson::Document doc(fromjson("{a: [{b: 0, c: 0}]}"));
    addIndexedPath("a");
    auto result = root.apply(getApplyParams(doc.root()));
    ASSERT_TRUE(result.indexesAffected);
    ASSERT_FALSE(result.noop);
    ASSERT_EQUALS(fromjson("{a: [{b: 1, c: 1}]}"), doc);
    ASSERT_TRUE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{$set: {'a.0.b': 1, 'a.0.c': 1}}"), getLogDoc());
}

TEST_F(UpdateArrayNodeTest, ApplyNestedArrayUpdates) {
    auto update = fromjson("{$set: {'a.$[i].b.$[j].c': 1, 'a.$[k].b.$[l].d': 1}}");
    auto arrayFilterI = fromjson("{'i.x': 0}");
    auto arrayFilterJ = fromjson("{'j.c': 0}");
    auto arrayFilterK = fromjson("{'k.x': 0}");
    auto arrayFilterL = fromjson("{'l.d': 0}");
    const CollatorInterface* collator = nullptr;
    std::map<StringData, std::unique_ptr<ExpressionWithPlaceholder>> arrayFilters;
    arrayFilters["i"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilterI, collator));
    arrayFilters["j"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilterJ, collator));
    arrayFilters["k"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilterK, collator));
    arrayFilters["l"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilterL, collator));
    std::set<std::string> foundIdentifiers;
    UpdateObjectNode root;
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[i].b.$[j].c"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[k].b.$[l].d"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));

    mutablebson::Document doc(fromjson("{a: [{x: 0, b: [{c: 0, d: 0}]}]}"));
    addIndexedPath("a");
    auto result = root.apply(getApplyParams(doc.root()));
    ASSERT_TRUE(result.indexesAffected);
    ASSERT_FALSE(result.noop);
    ASSERT_EQUALS(fromjson("{a: [{x: 0, b: [{c: 1, d: 1}]}]}"), doc);
    ASSERT_TRUE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{$set: {'a.0.b.0.c': 1, 'a.0.b.0.d': 1}}"), getLogDoc());
}

TEST_F(UpdateArrayNodeTest, ApplyUpdatesWithMergeConflictToArrayElementFails) {
    auto update = fromjson("{$set: {'a.$[i]': 1, 'a.$[j]': 1}}");
    auto arrayFilterI = fromjson("{'i': 0}");
    auto arrayFilterJ = fromjson("{'j': 0}");
    const CollatorInterface* collator = nullptr;
    std::map<StringData, std::unique_ptr<ExpressionWithPlaceholder>> arrayFilters;
    arrayFilters["i"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilterI, collator));
    arrayFilters["j"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilterJ, collator));
    std::set<std::string> foundIdentifiers;
    UpdateObjectNode root;
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[i]"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[j]"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));

    mutablebson::Document doc(fromjson("{a: [0]}"));
    addIndexedPath("a");
    ASSERT_THROWS_CODE_AND_WHAT(root.apply(getApplyParams(doc.root())),
                                UserException,
                                ErrorCodes::ConflictingUpdateOperators,
                                "Update created a conflict at 'a.0'");
}

TEST_F(UpdateArrayNodeTest, ApplyUpdatesWithEmptyIdentifiersWithMergeConflictToArrayElementFails) {
    auto update = fromjson("{$set: {'a.$[].b.$[i]': 1, 'a.$[].b.$[j]': 1}}");
    auto arrayFilterI = fromjson("{'i': 0}");
    auto arrayFilterJ = fromjson("{'j': 0}");
    const CollatorInterface* collator = nullptr;
    std::map<StringData, std::unique_ptr<ExpressionWithPlaceholder>> arrayFilters;
    arrayFilters["i"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilterI, collator));
    arrayFilters["j"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilterJ, collator));
    std::set<std::string> foundIdentifiers;
    UpdateObjectNode root;
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[].b.$[i]"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[].b.$[j]"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));

    mutablebson::Document doc(fromjson("{a: [{b: [0]}]}"));
    addIndexedPath("a");
    ASSERT_THROWS_CODE_AND_WHAT(root.apply(getApplyParams(doc.root())),
                                UserException,
                                ErrorCodes::ConflictingUpdateOperators,
                                "Update created a conflict at 'a.0.b.0'");
}

TEST_F(UpdateArrayNodeTest, ApplyNestedArrayUpdatesWithMergeConflictFails) {
    auto update = fromjson("{$set: {'a.$[i].b.$[j]': 1, 'a.$[k].b.$[l]': 1}}");
    auto arrayFilterI = fromjson("{'i.c': 0}");
    auto arrayFilterJ = fromjson("{j: 0}");
    auto arrayFilterK = fromjson("{'k.c': 0}");
    auto arrayFilterL = fromjson("{l: 0}");
    const CollatorInterface* collator = nullptr;
    std::map<StringData, std::unique_ptr<ExpressionWithPlaceholder>> arrayFilters;
    arrayFilters["i"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilterI, collator));
    arrayFilters["j"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilterJ, collator));
    arrayFilters["k"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilterK, collator));
    arrayFilters["l"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilterL, collator));
    std::set<std::string> foundIdentifiers;
    UpdateObjectNode root;
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[i].b.$[j]"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[k].b.$[l]"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));

    mutablebson::Document doc(fromjson("{a: [{b: [0], c: 0}]}"));
    addIndexedPath("a");
    ASSERT_THROWS_CODE_AND_WHAT(root.apply(getApplyParams(doc.root())),
                                UserException,
                                ErrorCodes::ConflictingUpdateOperators,
                                "Update created a conflict at 'a.0.b.0'");
}

TEST_F(UpdateArrayNodeTest, NoArrayElementsMatch) {
    auto update = fromjson("{$set: {'a.$[i]': 1}}");
    auto arrayFilter = fromjson("{'i': 0}");
    const CollatorInterface* collator = nullptr;
    std::map<StringData, std::unique_ptr<ExpressionWithPlaceholder>> arrayFilters;
    arrayFilters["i"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilter, collator));
    std::set<std::string> foundIdentifiers;
    UpdateObjectNode root;
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[i]"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));

    mutablebson::Document doc(fromjson("{a: [2, 2, 2]}"));
    addIndexedPath("a");
    auto result = root.apply(getApplyParams(doc.root()));
    ASSERT_FALSE(result.indexesAffected);
    ASSERT_TRUE(result.noop);
    ASSERT_EQUALS(fromjson("{a: [2, 2, 2]}"), doc);
    ASSERT_TRUE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{}"), getLogDoc());
}

TEST_F(UpdateArrayNodeTest, UpdatesToAllArrayElementsAreNoops) {
    auto update = fromjson("{$set: {'a.$[i]': 1}}");
    auto arrayFilter = fromjson("{'i': 0}");
    const CollatorInterface* collator = nullptr;
    std::map<StringData, std::unique_ptr<ExpressionWithPlaceholder>> arrayFilters;
    arrayFilters["i"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilter, collator));
    std::set<std::string> foundIdentifiers;
    UpdateObjectNode root;
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[i]"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));

    mutablebson::Document doc(fromjson("{a: [1, 1, 1]}"));
    addIndexedPath("a");
    auto result = root.apply(getApplyParams(doc.root()));
    ASSERT_FALSE(result.indexesAffected);
    ASSERT_TRUE(result.noop);
    ASSERT_EQUALS(fromjson("{a: [1, 1, 1]}"), doc);
    ASSERT_TRUE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{}"), getLogDoc());
}

TEST_F(UpdateArrayNodeTest, NoArrayElementAffectsIndexes) {
    auto update = fromjson("{$set: {'a.$[i].b': 0}}");
    auto arrayFilter = fromjson("{'i.c': 0}");
    const CollatorInterface* collator = nullptr;
    std::map<StringData, std::unique_ptr<ExpressionWithPlaceholder>> arrayFilters;
    arrayFilters["i"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilter, collator));
    std::set<std::string> foundIdentifiers;
    UpdateObjectNode root;
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[i].b"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));

    mutablebson::Document doc(fromjson("{a: [{c: 0}, {c: 0}, {c: 0}]}"));
    addIndexedPath("a.c");
    auto result = root.apply(getApplyParams(doc.root()));
    ASSERT_FALSE(result.indexesAffected);
    ASSERT_FALSE(result.noop);
    ASSERT_EQUALS(fromjson("{a: [{c: 0, b: 0}, {c: 0, b: 0}, {c: 0, b: 0}]}"), doc);
    ASSERT_FALSE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{$set: {a: [{c: 0, b: 0}, {c: 0, b: 0}, {c: 0, b: 0}]}}"), getLogDoc());
}

TEST_F(UpdateArrayNodeTest, WhenOneElementIsMatchedLogElementUpdateDirectly) {
    auto update = fromjson("{$set: {'a.$[i].b': 0}}");
    auto arrayFilter = fromjson("{'i.c': 0}");
    const CollatorInterface* collator = nullptr;
    std::map<StringData, std::unique_ptr<ExpressionWithPlaceholder>> arrayFilters;
    arrayFilters["i"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilter, collator));
    std::set<std::string> foundIdentifiers;
    UpdateObjectNode root;
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[i].b"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));

    mutablebson::Document doc(fromjson("{a: [{c: 1}, {c: 0}, {c: 1}]}"));
    addIndexedPath("a");
    auto result = root.apply(getApplyParams(doc.root()));
    ASSERT_TRUE(result.indexesAffected);
    ASSERT_FALSE(result.noop);
    ASSERT_EQUALS(fromjson("{a: [{c: 1}, {c: 0, b: 0}, {c: 1}]}"), doc);
    ASSERT_FALSE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{$set: {'a.1.b': 0}}"), getLogDoc());
}

TEST_F(UpdateArrayNodeTest, WhenOneElementIsModifiedLogElement) {
    auto update = fromjson("{$set: {'a.$[i].b': 0}}");
    auto arrayFilter = fromjson("{'i.c': 0}");
    const CollatorInterface* collator = nullptr;
    std::map<StringData, std::unique_ptr<ExpressionWithPlaceholder>> arrayFilters;
    arrayFilters["i"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilter, collator));
    std::set<std::string> foundIdentifiers;
    UpdateObjectNode root;
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[i].b"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));

    mutablebson::Document doc(fromjson("{a: [{c: 0, b: 0}, {c: 0}, {c: 1}]}"));
    addIndexedPath("a");
    auto result = root.apply(getApplyParams(doc.root()));
    ASSERT_TRUE(result.indexesAffected);
    ASSERT_FALSE(result.noop);
    ASSERT_EQUALS(fromjson("{a: [{c: 0, b: 0}, {c: 0, b: 0}, {c: 1}]}"), doc);
    ASSERT_FALSE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{$set: {'a.1': {c: 0, b: 0}}}"), getLogDoc());
}

TEST_F(UpdateArrayNodeTest, ArrayUpdateOnEmptyArrayIsANoop) {
    auto update = fromjson("{$set: {'a.$[]': 0}}");
    const CollatorInterface* collator = nullptr;
    std::map<StringData, std::unique_ptr<ExpressionWithPlaceholder>> arrayFilters;
    std::set<std::string> foundIdentifiers;
    UpdateObjectNode root;
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[]"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));

    mutablebson::Document doc(fromjson("{a: []}"));
    addIndexedPath("a");
    auto result = root.apply(getApplyParams(doc.root()));
    ASSERT_FALSE(result.indexesAffected);
    ASSERT_TRUE(result.noop);
    ASSERT_EQUALS(fromjson("{a: []}"), doc);
    ASSERT_TRUE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{}"), getLogDoc());
}

TEST_F(UpdateArrayNodeTest, ApplyPositionalInsideArrayUpdate) {
    auto update = fromjson("{$set: {'a.$[i].b.$': 1}}");
    auto arrayFilter = fromjson("{'i.c': 0}");
    const CollatorInterface* collator = nullptr;
    std::map<StringData, std::unique_ptr<ExpressionWithPlaceholder>> arrayFilters;
    arrayFilters["i"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilter, collator));
    std::set<std::string> foundIdentifiers;
    UpdateObjectNode root;
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[i].b.$"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));

    mutablebson::Document doc(fromjson("{a: [{b: [0, 0], c: 0}]}"));
    addIndexedPath("a");
    setMatchedField("1");
    auto result = root.apply(getApplyParams(doc.root()));
    ASSERT_TRUE(result.indexesAffected);
    ASSERT_FALSE(result.noop);
    ASSERT_EQUALS(fromjson("{a: [{b: [0, 1], c: 0}]}"), doc);
    ASSERT_TRUE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{$set: {'a.0.b.1': 1}}"), getLogDoc());
}

TEST_F(UpdateArrayNodeTest, ApplyArrayUpdateFromReplication) {
    auto update = fromjson("{$set: {'a.$[i].b': 1}}");
    auto arrayFilter = fromjson("{'i': 0}");
    const CollatorInterface* collator = nullptr;
    std::map<StringData, std::unique_ptr<ExpressionWithPlaceholder>> arrayFilters;
    arrayFilters["i"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilter, collator));
    std::set<std::string> foundIdentifiers;
    UpdateObjectNode root;
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[i].b"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));

    mutablebson::Document doc(fromjson("{a: [0]}"));
    addIndexedPath("a");
    setFromReplication(true);
    auto result = root.apply(getApplyParams(doc.root()));
    ASSERT_FALSE(result.indexesAffected);
    ASSERT_TRUE(result.noop);
    ASSERT_EQUALS(fromjson("{a: [0]}"), doc);
    ASSERT_TRUE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{}"), getLogDoc());
}

TEST_F(UpdateArrayNodeTest, ApplyArrayUpdateNotFromReplication) {
    auto update = fromjson("{$set: {'a.$[i].b': 1}}");
    auto arrayFilter = fromjson("{'i': 0}");
    const CollatorInterface* collator = nullptr;
    std::map<StringData, std::unique_ptr<ExpressionWithPlaceholder>> arrayFilters;
    arrayFilters["i"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilter, collator));
    std::set<std::string> foundIdentifiers;
    UpdateObjectNode root;
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[i].b"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));

    mutablebson::Document doc(fromjson("{a: [0]}"));
    addIndexedPath("a");
    ASSERT_THROWS_CODE_AND_WHAT(root.apply(getApplyParams(doc.root())),
                                UserException,
                                ErrorCodes::PathNotViable,
                                "Cannot create field 'b' in element {0: 0}");
}

TEST_F(UpdateArrayNodeTest, ApplyArrayUpdateWithoutLogBuilderOrIndexData) {
    auto update = fromjson("{$set: {'a.$[i]': 1}}");
    auto arrayFilter = fromjson("{'i': 0}");
    const CollatorInterface* collator = nullptr;
    std::map<StringData, std::unique_ptr<ExpressionWithPlaceholder>> arrayFilters;
    arrayFilters["i"] = uassertStatusOK(ExpressionWithPlaceholder::parse(arrayFilter, collator));
    std::set<std::string> foundIdentifiers;
    UpdateObjectNode root;
    ASSERT_OK(UpdateObjectNode::parseAndMerge(&root,
                                              modifiertable::ModifierType::MOD_SET,
                                              update["$set"]["a.$[i]"],
                                              collator,
                                              arrayFilters,
                                              foundIdentifiers));

    mutablebson::Document doc(fromjson("{a: [0]}"));
    setLogBuilderToNull();
    auto result = root.apply(getApplyParams(doc.root()));
    ASSERT_FALSE(result.indexesAffected);
    ASSERT_FALSE(result.noop);
    ASSERT_EQUALS(fromjson("{a: [1]}"), doc);
    ASSERT_TRUE(doc.isInPlaceModeEnabled());
}

}  // namespace
}  // namespace mongo
