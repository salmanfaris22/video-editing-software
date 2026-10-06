#include "core/BoundedQueue.h"
#include "core/SpscRing.h"

#include <gtest/gtest.h>

#include <atomic>
#include <numeric>
#include <thread>
#include <vector>

using namespace lectern;

TEST(BoundedQueue, TryPushRespectsCapacityAndKeepsItemOnFailure) {
    BoundedQueue<std::string> q(2);
    std::string a = "a", b = "b", c = "c";
    EXPECT_EQ(q.tryPush(a), PushResult::Ok);
    EXPECT_EQ(q.tryPush(b), PushResult::Ok);
    EXPECT_EQ(q.tryPush(c), PushResult::Full);
    EXPECT_EQ(c, "c");  // not moved from on failure
    EXPECT_EQ(q.tryPop().value(), "a");
    EXPECT_EQ(q.tryPush(c), PushResult::Ok);
    q.close();
    std::string d = "d";
    EXPECT_EQ(q.tryPush(d), PushResult::Closed);
    EXPECT_EQ(q.pop().value(), "b");  // drains after close
    EXPECT_EQ(q.pop().value(), "c");
    EXPECT_FALSE(q.pop().has_value());
}

TEST(BoundedQueue, CostBasedCapacityAcceptsOversizedItemWhenEmpty) {
    struct Bytes {
        std::size_t operator()(const std::vector<char>& v) const { return v.size(); }
    };
    BoundedQueue<std::vector<char>, Bytes> q(100);
    std::vector<char> big(500);
    EXPECT_EQ(q.tryPush(big), PushResult::Ok);  // would deadlock otherwise
    std::vector<char> small(10);
    EXPECT_EQ(q.tryPush(small), PushResult::Full);
    q.tryPop();
    EXPECT_EQ(q.tryPush(small), PushResult::Ok);
    EXPECT_EQ(q.usedCost(), 10u);
}

TEST(BoundedQueue, ProducerConsumerDeliversEverythingInOrder) {
    BoundedQueue<int> q(8);
    constexpr int kCount = 20000;
    std::thread producer([&] {
        for (int i = 0; i < kCount; ++i) {
            int v = i;
            ASSERT_EQ(q.push(v), PushResult::Ok);
        }
        q.close();
    });
    int expected = 0;
    while (auto v = q.pop()) {
        ASSERT_EQ(*v, expected);
        ++expected;
    }
    producer.join();
    EXPECT_EQ(expected, kCount);
}

TEST(SpscRing, WrapsAroundAndPreservesData) {
    SpscRing<float> ring(8);
    EXPECT_EQ(ring.capacity(), 8u);
    float in[6] = {1, 2, 3, 4, 5, 6};
    float out[8] = {};
    EXPECT_EQ(ring.write(in, 6), 6u);
    EXPECT_EQ(ring.read(out, 4), 4u);
    EXPECT_EQ(ring.write(in, 6), 6u);  // wraps
    EXPECT_EQ(ring.write(in, 1), 0u);  // full
    EXPECT_EQ(ring.read(out, 8), 8u);
    const float expected[8] = {5, 6, 1, 2, 3, 4, 5, 6};
    for (int i = 0; i < 8; ++i) EXPECT_EQ(out[i], expected[i]);
}

TEST(SpscRing, WriteAvailableIsNeverStale) {
    // Regression: writeAvailable() used to refresh the consumer index only
    // when the ring looked completely full, under-reporting free space.
    SpscRing<float> ring(16);
    float data[16] = {};
    ASSERT_EQ(ring.write(data, 12), 12u);
    EXPECT_EQ(ring.writeAvailable(), 4u);
    float out[12];
    ASSERT_EQ(ring.read(out, 12), 12u);  // consumer frees space
    EXPECT_EQ(ring.writeAvailable(), 16u);
    EXPECT_EQ(ring.readAvailable(), 0u);
}

TEST(SpscRing, ConcurrentProducerConsumer) {
    SpscRing<std::uint32_t> ring(1024);
    constexpr std::uint32_t kTotal = 2'000'000;
    std::thread producer([&] {
        std::uint32_t next = 0;
        std::uint32_t chunk[37];
        while (next < kTotal) {
            const std::uint32_t n = std::min<std::uint32_t>(37, kTotal - next);
            for (std::uint32_t i = 0; i < n; ++i) chunk[i] = next + i;
            next += static_cast<std::uint32_t>(ring.write(chunk, n));
        }
    });
    std::uint32_t expected = 0;
    std::uint32_t buf[64];
    while (expected < kTotal) {
        const std::size_t got = ring.read(buf, 64);
        for (std::size_t i = 0; i < got; ++i) ASSERT_EQ(buf[i], expected++);
    }
    producer.join();
}
