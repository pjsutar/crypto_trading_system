#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "OrderBook.hpp"

using namespace cts::data;

// Helpers

// Build a simple snapshot: one bid at bid_price, one ask at ask_price
static void apply_basic_snapshot(
    OrderBook&  book,
    double      bid_price,
    double      bid_qty,
    double      ask_price,
    double      ask_qty,
    uint64_t    last_update_id = 100
) {
    book.apply_snapshot(
        last_update_id,
        {{bid_price, bid_qty}},
        {{ask_price, ask_qty}}
    );
}

// Tests

TEST_CASE("snapshot initializes book correctly") {
    OrderBook book;

    apply_basic_snapshot(book, 43250.0, 2.5, 43251.0, 1.2);

    CHECK(book.is_initialized());

    BookSnapshot snap = book.get_snapshot(0);
    CHECK(snap.best_bid  == doctest::Approx(43250.0));
    CHECK(snap.best_ask  == doctest::Approx(43251.0));
    CHECK(snap.mid_price == doctest::Approx(43250.5));

    // Top level quantities
    CHECK(snap.bids[0].quantity == doctest::Approx(2.5));
    CHECK(snap.asks[0].quantity == doctest::Approx(1.2));
}

TEST_CASE("diff adds a new price level") {
    OrderBook book;
    apply_basic_snapshot(book, 43250.0, 2.5, 43251.0, 1.2, 100);

    // Add a second bid level at 43249.0
    bool ok = book.apply_diff(
        101, 102,
        {{43249.0, 3.0}},   // new bid level
        {}
    );

    CHECK(ok);

    BookSnapshot snap = book.get_snapshot(0);
    // Best bid should still be 43250.0
    CHECK(snap.bids[0].price == doctest::Approx(43250.0));
    // Second best bid should be the new level
    CHECK(snap.bids[1].price    == doctest::Approx(43249.0));
    CHECK(snap.bids[1].quantity == doctest::Approx(3.0));
}

TEST_CASE("diff with qty zero removes price level") {
    OrderBook book;
    apply_basic_snapshot(book, 43250.0, 2.5, 43251.0, 1.2, 100);

    // Remove the bid at 43250.0 by sending qty == 0
    bool ok = book.apply_diff(
        101, 102,
        {{43250.0, 0.0}},   // qty 0 = remove this level
        {}
    );

    CHECK(ok);

    BookSnapshot snap = book.get_snapshot(0);
    // The removed level must not appear as best bid
    CHECK(snap.bids[0].price != doctest::Approx(43250.0));
}

TEST_CASE("diff with sequence gap returns false") {
    OrderBook book;
    apply_basic_snapshot(book, 43250.0, 2.5, 43251.0, 1.2, 100);

    // First valid event
    book.apply_diff(101, 103, {}, {});

    // Next event skips 104 — gap between 103 and 105
    bool ok = book.apply_diff(105, 107, {}, {});

    CHECK_FALSE(ok);
    // Book should be marked uninitialized after a gap
    CHECK_FALSE(book.is_initialized());
}

TEST_CASE("first diff event satisfies U <= lastUpdateId+1 <= u") {
    OrderBook book;
    // Snapshot lastUpdateId = 100
    apply_basic_snapshot(book, 43250.0, 2.5, 43251.0, 1.2, 100);

    // Valid first event: U=101, u=103
    // Satisfies U(101) <= lastUpdateId+1(101) <= u(103)
    bool ok = book.apply_diff(101, 103, {}, {});
    CHECK(ok);
}

TEST_CASE("stale diff events before snapshot are silently dropped") {
    OrderBook book;
    // Snapshot lastUpdateId = 100
    apply_basic_snapshot(book, 43250.0, 2.5, 43251.0, 1.2, 100);

    // This event is fully covered by the snapshot — u <= lastUpdateId
    bool ok = book.apply_diff(95, 99, {}, {});

    // Should return true (not an error) and book remains valid
    CHECK(ok);
    CHECK(book.is_initialized());
}

TEST_CASE("snapshot wipes previous state") {
    OrderBook book;
    apply_basic_snapshot(book, 43250.0, 2.5, 43251.0, 1.2, 100);

    // Apply a completely different snapshot
    book.apply_snapshot(
        200,
        {{50000.0, 1.0}},
        {{50001.0, 2.0}}
    );

    BookSnapshot snap = book.get_snapshot(0);
    CHECK(snap.best_bid == doctest::Approx(50000.0));
    CHECK(snap.best_ask == doctest::Approx(50001.0));
}