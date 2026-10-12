#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <vector>
#include "../order_book/order_book.h"

using Tree = OrderBook::RedBlackTree;
using Color = PriceLevel::Color;

namespace {

// Works whether the tree uses nullptr or a real sentinel node as nil.
bool isNil(const Tree& t, const PriceLevel* p) {
    return p == nullptr || p == t.getNil();
}

// Returns black height of the subtree, or -1 after recording an error.
int checkSubtree(const Tree& t, PriceLevel* node, PriceLevel* expectedParent,
                 const std::int32_t* lo, const std::int32_t* hi,
                 std::vector<std::int32_t>& inorder, std::ostringstream& err) {
    if (isNil(t, node)) return 1;
    std::int32_t p = node->getPrice();
    if (node->getParent() != expectedParent &&
        !(isNil(t, expectedParent) && isNil(t, node->getParent()))) {
        err << "node " << p << " has wrong parent pointer; ";
    }
    if ((lo && p <= *lo) || (hi && p >= *hi)) {
        err << "BST order violated at node " << p << "; ";
    }
    if (node->getColor() == Color::RED) {
        for (PriceLevel* c : {node->getLeftChild(), node->getRightChild()}) {
            if (!isNil(t, c) && c->getColor() == Color::RED) {
                err << "red node " << p << " has red child " << c->getPrice() << "; ";
            }
        }
    }
    int lh = checkSubtree(t, node->getLeftChild(), node, lo, &p, inorder, err);
    inorder.push_back(p);
    int rh = checkSubtree(t, node->getRightChild(), node, &p, hi, inorder, err);
    if (lh != rh) {
        err << "black height differs under node " << p << " (" << lh << " vs " << rh << "); ";
    }
    return lh + (node->getColor() == Color::BLACK ? 1 : 0);
}

int height(const Tree& t, PriceLevel* n) {
    if (isNil(t, n)) return 0;
    return 1 + std::max(height(t, n->getLeftChild()), height(t, n->getRightChild()));
}

// Checks every red-black rule, parent links, and that the tree holds exactly `expected`.
::testing::AssertionResult isValidTree(const Tree& t, const std::set<std::int32_t>& expected) {
    std::ostringstream err;
    PriceLevel* root = t.getRoot();
    if (!isNil(t, root)) {
        if (root->getColor() != Color::BLACK) err << "root is not black; ";
        if (!isNil(t, root->getParent())) err << "root has a parent; ";
    }
    std::vector<std::int32_t> inorder;
    checkSubtree(t, root, root ? root->getParent() : nullptr, nullptr, nullptr, inorder, err);
    std::vector<std::int32_t> want(expected.begin(), expected.end());
    if (inorder != want) {
        err << "in-order walk has " << inorder.size() << " keys, expected " << want.size()
            << " (contents differ); ";
    }
    std::size_t n = expected.size();
    if (n > 0 && height(t, root) > 2 * std::log2(static_cast<double>(n) + 1) + 1e-9) {
        err << "height " << height(t, root) << " exceeds 2*log2(n+1) for n=" << n << "; ";
    }
    std::string s = err.str();
    if (s.empty()) return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure() << s;
}

// Owns the nodes the tree allocates, since remove() only unlinks.
struct TreeFixture {
    Tree tree;
    std::map<std::int32_t, PriceLevel*> nodes;
    std::set<std::int32_t> keys;

    PriceLevel* insert(std::int32_t price) {
        PriceLevel* n = tree.insert(price);
        nodes[price] = n;
        keys.insert(price);
        return n;
    }
    void remove(std::int32_t price) {
        PriceLevel* n = nodes.at(price);
        tree.remove(n);
        nodes.erase(price);
        keys.erase(price);
        delete n;
    }
    ~TreeFixture() {
        for (auto& [_, n] : nodes) delete n;
    }
};

} // namespace

TEST(RedBlackTree, EmptyTreeHasNilRoot) {
    Tree t;
    EXPECT_TRUE(isNil(t, t.getRoot()));
    EXPECT_TRUE(isValidTree(t, {}));
}

TEST(RedBlackTree, InsertReturnsNodeWithThatPrice) {
    TreeFixture f;
    PriceLevel* n = f.insert(100);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->getPrice(), 100);
    EXPECT_TRUE(n->isEmpty());
}

TEST(RedBlackTree, SingleInsertBecomesBlackRoot) {
    TreeFixture f;
    PriceLevel* n = f.insert(100);
    EXPECT_EQ(f.tree.getRoot(), n);
    EXPECT_TRUE(isValidTree(f.tree, f.keys));
}

TEST(RedBlackTree, AscendingInsertsStayBalanced) {
    TreeFixture f;
    for (int p = 1; p <= 1000; ++p) {
        f.insert(p);
        ASSERT_TRUE(isValidTree(f.tree, f.keys)) << "after inserting " << p;
    }
}

TEST(RedBlackTree, DescendingInsertsStayBalanced) {
    TreeFixture f;
    for (int p = 1000; p >= 1; --p) {
        f.insert(p);
        ASSERT_TRUE(isValidTree(f.tree, f.keys)) << "after inserting " << p;
    }
}

TEST(RedBlackTree, RandomInsertsStayBalanced) {
    TreeFixture f;
    std::vector<int> prices(2000);
    for (int i = 0; i < 2000; ++i) prices[i] = i * 3 + 1;
    std::shuffle(prices.begin(), prices.end(), std::mt19937(42));
    for (int p : prices) {
        f.insert(p);
        ASSERT_TRUE(isValidTree(f.tree, f.keys)) << "after inserting " << p;
    }
}

TEST(RedBlackTree, RemoveOnlyNodeEmptiesTree) {
    TreeFixture f;
    f.insert(100);
    f.remove(100);
    EXPECT_TRUE(isNil(f.tree, f.tree.getRoot()));
    EXPECT_TRUE(isValidTree(f.tree, f.keys));
}

TEST(RedBlackTree, RemoveLeaf) {
    TreeFixture f;
    for (int p : {50, 20, 80, 10, 30, 70, 90}) f.insert(p);
    f.remove(10);
    EXPECT_TRUE(isValidTree(f.tree, f.keys));
}

TEST(RedBlackTree, RemoveNodeWithOneChild) {
    TreeFixture f;
    for (int p : {50, 20, 80, 10}) f.insert(p);
    f.remove(20);
    EXPECT_TRUE(isValidTree(f.tree, f.keys));
}

TEST(RedBlackTree, RemoveNodeWithTwoChildren) {
    TreeFixture f;
    for (int p : {50, 20, 80, 10, 30, 70, 90, 25, 35}) f.insert(p);
    f.remove(20);
    EXPECT_TRUE(isValidTree(f.tree, f.keys));
}

TEST(RedBlackTree, RemoveRootRepeatedly) {
    TreeFixture f;
    for (int p = 1; p <= 200; ++p) f.insert(p);
    while (!f.keys.empty()) {
        f.remove(f.tree.getRoot()->getPrice());
        ASSERT_TRUE(isValidTree(f.tree, f.keys)) << "with " << f.keys.size() << " keys left";
    }
}

// Pattern of a sell side being consumed: lowest price removed each time.
TEST(RedBlackTree, RemoveMinimumRepeatedly) {
    TreeFixture f;
    for (int p = 1; p <= 500; ++p) f.insert(p);
    while (!f.keys.empty()) {
        f.remove(*f.keys.begin());
        ASSERT_TRUE(isValidTree(f.tree, f.keys)) << "with " << f.keys.size() << " keys left";
    }
}

// Pattern of a buy side being consumed: highest price removed each time.
TEST(RedBlackTree, RemoveMaximumRepeatedly) {
    TreeFixture f;
    for (int p = 1; p <= 500; ++p) f.insert(p);
    while (!f.keys.empty()) {
        f.remove(*f.keys.rbegin());
        ASSERT_TRUE(isValidTree(f.tree, f.keys)) << "with " << f.keys.size() << " keys left";
    }
}

TEST(RedBlackTree, ReinsertAfterRemove) {
    TreeFixture f;
    for (int p : {50, 20, 80}) f.insert(p);
    f.remove(20);
    f.insert(20);
    EXPECT_TRUE(isValidTree(f.tree, f.keys));
}

TEST(RedBlackTree, RandomInsertRemoveMix) {
    TreeFixture f;
    std::mt19937 rng(7);
    std::uniform_int_distribution<int> price(1, 300);
    for (int op = 0; op < 5000; ++op) {
        int p = price(rng);
        if (f.keys.count(p)) {
            f.remove(p);
        } else {
            f.insert(p);
        }
        ASSERT_TRUE(isValidTree(f.tree, f.keys)) << "after op " << op << " on price " << p;
    }
}
