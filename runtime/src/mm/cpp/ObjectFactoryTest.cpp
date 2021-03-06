/*
 * Copyright 2010-2020 JetBrains s.r.o. Use of this source code is governed by the Apache 2.0 license
 * that can be found in the LICENSE file.
 */

#include "ObjectFactory.hpp"

#include <atomic>
#include <thread>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "CppSupport.hpp"
#include "TestSupport.hpp"

using namespace kotlin;

template <size_t DataAlignment>
using ObjectFactoryStorage = mm::internal::ObjectFactoryStorage<DataAlignment>;

using ObjectFactoryStorageRegular = ObjectFactoryStorage<alignof(void*)>;

namespace {

template <size_t DataAlignment>
std::vector<void*> Collect(ObjectFactoryStorage<DataAlignment>& storage) {
    std::vector<void*> result;
    for (auto& node : storage.Iter()) {
        result.push_back(node.Data());
    }
    return result;
}

template <typename T, size_t DataAlignment>
std::vector<T> Collect(ObjectFactoryStorage<DataAlignment>& storage) {
    std::vector<T> result;
    for (auto& node : storage.Iter()) {
        result.push_back(*static_cast<T*>(node.Data()));
    }
    return result;
}

struct MoveOnlyImpl : private MoveOnly {
    MoveOnlyImpl(int value1, int value2) : value1(value1), value2(value2) {}

    int value1;
    int value2;
};

struct PinnedImpl : private Pinned {
    PinnedImpl(int value1, int value2, int value3) : value1(value1), value2(value2), value3(value3) {}

    int value1;
    int value2;
    int value3;
};

struct MaxAlignedData {
    explicit MaxAlignedData(int value) : value(value) {}

    std::max_align_t padding;
    int value;
};

} // namespace

TEST(ObjectFactoryStorageTest, Empty) {
    ObjectFactoryStorageRegular storage;

    auto actual = Collect(storage);

    EXPECT_THAT(actual, testing::IsEmpty());
}

TEST(ObjectFactoryStorageTest, DoNotPublish) {
    ObjectFactoryStorageRegular storage;
    ObjectFactoryStorageRegular::Producer producer(storage);

    producer.Insert<int>(1);
    producer.Insert<int>(2);

    auto actual = Collect(storage);

    EXPECT_THAT(actual, testing::IsEmpty());
}

TEST(ObjectFactoryStorageTest, Publish) {
    ObjectFactoryStorageRegular storage;
    ObjectFactoryStorageRegular::Producer producer1(storage);
    ObjectFactoryStorageRegular::Producer producer2(storage);

    producer1.Insert<int>(1);
    producer1.Insert<int>(2);
    producer2.Insert<int>(10);
    producer2.Insert<int>(20);

    producer1.Publish();
    producer2.Publish();

    auto actual = Collect<int>(storage);

    EXPECT_THAT(actual, testing::ElementsAre(1, 2, 10, 20));
}

TEST(ObjectFactoryStorageTest, PublishDifferentTypes) {
    ObjectFactoryStorage<alignof(MaxAlignedData)> storage;
    ObjectFactoryStorage<alignof(MaxAlignedData)>::Producer producer(storage);

    producer.Insert<int>(1);
    producer.Insert<size_t>(2);
    producer.Insert<MoveOnlyImpl>(3, 4);
    producer.Insert<PinnedImpl>(5, 6, 7);
    producer.Insert<MaxAlignedData>(8);

    producer.Publish();

    auto actual = storage.Iter();
    auto it = actual.begin();
    EXPECT_THAT(it->Data<int>(), 1);
    ++it;
    EXPECT_THAT(it->Data<size_t>(), 2);
    ++it;
    auto& moveOnly = it->Data<MoveOnlyImpl>();
    EXPECT_THAT(moveOnly.value1, 3);
    EXPECT_THAT(moveOnly.value2, 4);
    ++it;
    auto& pinned = it->Data<PinnedImpl>();
    EXPECT_THAT(pinned.value1, 5);
    EXPECT_THAT(pinned.value2, 6);
    EXPECT_THAT(pinned.value3, 7);
    ++it;
    auto& maxAlign = it->Data<MaxAlignedData>();
    EXPECT_THAT(maxAlign.value, 8);
    ++it;
    EXPECT_THAT(it, actual.end());
}

TEST(ObjectFactoryStorageTest, PublishSeveralTimes) {
    ObjectFactoryStorageRegular storage;
    ObjectFactoryStorageRegular::Producer producer(storage);

    // Add 2 elements and publish.
    producer.Insert<int>(1);
    producer.Insert<int>(2);
    producer.Publish();

    // Add another element and publish.
    producer.Insert<int>(3);
    producer.Publish();

    // Publish without adding elements.
    producer.Publish();

    // Add yet another two elements and publish.
    producer.Insert<int>(4);
    producer.Insert<int>(5);
    producer.Publish();

    auto actual = Collect<int>(storage);

    EXPECT_THAT(actual, testing::ElementsAre(1, 2, 3, 4, 5));
}

TEST(ObjectFactoryStorageTest, PublishInDestructor) {
    ObjectFactoryStorageRegular storage;

    {
        ObjectFactoryStorageRegular::Producer producer(storage);
        producer.Insert<int>(1);
        producer.Insert<int>(2);
    }

    auto actual = Collect<int>(storage);

    EXPECT_THAT(actual, testing::ElementsAre(1, 2));
}

TEST(ObjectFactoryStorageTest, EraseFirst) {
    ObjectFactoryStorageRegular storage;
    ObjectFactoryStorageRegular::Producer producer(storage);

    producer.Insert<int>(1);
    producer.Insert<int>(2);
    producer.Insert<int>(3);

    producer.Publish();

    {
        auto iter = storage.Iter();
        for (auto it = iter.begin(); it != iter.end();) {
            if (it->Data<int>() == 1) {
                iter.EraseAndAdvance(it);
            } else {
                ++it;
            }
        }
    }

    auto actual = Collect<int>(storage);

    EXPECT_THAT(actual, testing::ElementsAre(2, 3));
}

TEST(ObjectFactoryStorageTest, EraseMiddle) {
    ObjectFactoryStorageRegular storage;
    ObjectFactoryStorageRegular::Producer producer(storage);

    producer.Insert<int>(1);
    producer.Insert<int>(2);
    producer.Insert<int>(3);

    producer.Publish();

    {
        auto iter = storage.Iter();
        for (auto it = iter.begin(); it != iter.end();) {
            if (it->Data<int>() == 2) {
                iter.EraseAndAdvance(it);
            } else {
                ++it;
            }
        }
    }

    auto actual = Collect<int>(storage);

    EXPECT_THAT(actual, testing::ElementsAre(1, 3));
}

TEST(ObjectFactoryStorageTest, EraseLast) {
    ObjectFactoryStorageRegular storage;
    ObjectFactoryStorageRegular::Producer producer(storage);

    producer.Insert<int>(1);
    producer.Insert<int>(2);
    producer.Insert<int>(3);

    producer.Publish();

    {
        auto iter = storage.Iter();
        for (auto it = iter.begin(); it != iter.end();) {
            if (it->Data<int>() == 3) {
                iter.EraseAndAdvance(it);
            } else {
                ++it;
            }
        }
    }

    auto actual = Collect<int>(storage);

    EXPECT_THAT(actual, testing::ElementsAre(1, 2));
}

TEST(ObjectFactoryStorageTest, EraseAll) {
    ObjectFactoryStorageRegular storage;
    ObjectFactoryStorageRegular::Producer producer(storage);

    producer.Insert<int>(1);
    producer.Insert<int>(2);
    producer.Insert<int>(3);

    producer.Publish();

    {
        auto iter = storage.Iter();
        for (auto it = iter.begin(); it != iter.end();) {
            iter.EraseAndAdvance(it);
        }
    }

    auto actual = Collect<int>(storage);

    EXPECT_THAT(actual, testing::IsEmpty());
}

TEST(ObjectFactoryStorageTest, EraseTheOnlyElement) {
    ObjectFactoryStorageRegular storage;
    ObjectFactoryStorageRegular::Producer producer(storage);

    producer.Insert<int>(1);

    producer.Publish();

    {
        auto iter = storage.Iter();
        auto it = iter.begin();
        iter.EraseAndAdvance(it);
    }

    auto actual = Collect<int>(storage);

    EXPECT_THAT(actual, testing::IsEmpty());
}

TEST(ObjectFactoryStorageTest, ConcurrentPublish) {
    ObjectFactoryStorageRegular storage;
    constexpr int kThreadCount = kDefaultThreadCount;
    std::atomic<bool> canStart(false);
    std::atomic<int> readyCount(0);
    std::vector<std::thread> threads;
    std::vector<int> expected;

    for (int i = 0; i < kThreadCount; ++i) {
        expected.push_back(i);
        threads.emplace_back([i, &storage, &canStart, &readyCount]() {
            ObjectFactoryStorageRegular::Producer producer(storage);
            producer.Insert<int>(i);
            ++readyCount;
            while (!canStart) {
            }
            producer.Publish();
        });
    }

    while (readyCount < kThreadCount) {
    }
    canStart = true;
    for (auto& t : threads) {
        t.join();
    }

    auto actual = Collect<int>(storage);

    EXPECT_THAT(actual, testing::UnorderedElementsAreArray(expected));
}

TEST(ObjectFactoryStorageTest, IterWhileConcurrentPublish) {
    ObjectFactoryStorageRegular storage;
    constexpr int kStartCount = 50;
    constexpr int kThreadCount = kDefaultThreadCount;

    std::vector<int> expectedBefore;
    std::vector<int> expectedAfter;
    ObjectFactoryStorageRegular::Producer producer(storage);
    for (int i = 0; i < kStartCount; ++i) {
        expectedBefore.push_back(i);
        expectedAfter.push_back(i);
        producer.Insert<int>(i);
    }
    producer.Publish();

    std::atomic<bool> canStart(false);
    std::atomic<int> readyCount(0);
    std::atomic<int> startedCount(0);
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreadCount; ++i) {
        int j = i + kStartCount;
        expectedAfter.push_back(j);
        threads.emplace_back([j, &storage, &canStart, &startedCount, &readyCount]() {
            ObjectFactoryStorageRegular::Producer producer(storage);
            producer.Insert<int>(j);
            ++readyCount;
            while (!canStart) {
            }
            ++startedCount;
            producer.Publish();
        });
    }

    std::vector<int> actualBefore;
    {
        auto iter = storage.Iter();
        while (readyCount < kThreadCount) {
        }
        canStart = true;
        while (startedCount < kThreadCount) {
        }

        for (auto& node : iter) {
            int element = *static_cast<int*>(node.Data());
            actualBefore.push_back(element);
        }
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_THAT(actualBefore, testing::ElementsAreArray(expectedBefore));

    auto actualAfter = Collect<int>(storage);

    EXPECT_THAT(actualAfter, testing::UnorderedElementsAreArray(expectedAfter));
}

TEST(ObjectFactoryStorageTest, EraseWhileConcurrentPublish) {
    ObjectFactoryStorageRegular storage;
    constexpr int kStartCount = 50;
    constexpr int kThreadCount = kDefaultThreadCount;

    std::vector<int> expectedAfter;
    ObjectFactoryStorageRegular::Producer producer(storage);
    for (int i = 0; i < kStartCount; ++i) {
        if (i % 2 == 0) {
            expectedAfter.push_back(i);
        }
        producer.Insert<int>(i);
    }
    producer.Publish();

    std::atomic<bool> canStart(false);
    std::atomic<int> readyCount(0);
    std::atomic<int> startedCount(0);
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreadCount; ++i) {
        int j = i + kStartCount;
        expectedAfter.push_back(j);
        threads.emplace_back([j, &storage, &canStart, &startedCount, &readyCount]() {
            ObjectFactoryStorageRegular::Producer producer(storage);
            producer.Insert<int>(j);
            ++readyCount;
            while (!canStart) {
            }
            ++startedCount;
            producer.Publish();
        });
    }

    {
        auto iter = storage.Iter();
        while (readyCount < kThreadCount) {
        }
        canStart = true;
        while (startedCount < kThreadCount) {
        }

        for (auto it = iter.begin(); it != iter.end();) {
            if (it->Data<int>() % 2 != 0) {
                iter.EraseAndAdvance(it);
            } else {
                ++it;
            }
        }
    }

    for (auto& t : threads) {
        t.join();
    }

    auto actual = Collect<int>(storage);

    EXPECT_THAT(actual, testing::UnorderedElementsAreArray(expectedAfter));
}

using mm::ObjectFactory;

namespace {

std::unique_ptr<TypeInfo> MakeObjectTypeInfo(int32_t size) {
    auto typeInfo = std_support::make_unique<TypeInfo>();
    typeInfo->typeInfo_ = typeInfo.get();
    typeInfo->instanceSize_ = size;
    return typeInfo;
}

std::unique_ptr<TypeInfo> MakeArrayTypeInfo(int32_t elementSize) {
    auto typeInfo = std_support::make_unique<TypeInfo>();
    typeInfo->typeInfo_ = typeInfo.get();
    typeInfo->instanceSize_ = -elementSize;
    return typeInfo;
}

} // namespace

TEST(ObjectFactoryTest, CreateObject) {
    auto typeInfo = MakeObjectTypeInfo(24);
    ObjectFactory objectFactory;
    ObjectFactory::ThreadQueue threadQueue(objectFactory);

    auto* object = threadQueue.CreateObject(typeInfo.get());
    threadQueue.Publish();

    auto iter = objectFactory.Iter();
    auto it = iter.begin();
    EXPECT_FALSE(it.IsArray());
    EXPECT_THAT(it.GetObjHeader(), object);
    ++it;
    EXPECT_THAT(it, iter.end());
}

TEST(ObjectFactoryTest, CreateArray) {
    auto typeInfo = MakeArrayTypeInfo(24);
    ObjectFactory objectFactory;
    ObjectFactory::ThreadQueue threadQueue(objectFactory);

    auto* array = threadQueue.CreateArray(typeInfo.get(), 3);
    threadQueue.Publish();

    auto iter = objectFactory.Iter();
    auto it = iter.begin();
    EXPECT_TRUE(it.IsArray());
    EXPECT_THAT(it.GetArrayHeader(), array);
    ++it;
    EXPECT_THAT(it, iter.end());
}

TEST(ObjectFactoryTest, Erase) {
    auto objectTypeInfo = MakeObjectTypeInfo(24);
    auto arrayTypeInfo = MakeArrayTypeInfo(24);
    ObjectFactory objectFactory;
    ObjectFactory::ThreadQueue threadQueue(objectFactory);

    for (int i = 0; i < 10; ++i) {
        threadQueue.CreateObject(objectTypeInfo.get());
        threadQueue.CreateArray(arrayTypeInfo.get(), 3);
    }

    threadQueue.Publish();

    {
        auto iter = objectFactory.Iter();
        for (auto it = iter.begin(); it != iter.end();) {
            if (it.IsArray()) {
                iter.EraseAndAdvance(it);
            } else {
                ++it;
            }
        }
    }

    {
        auto iter = objectFactory.Iter();
        int count = 0;
        for (auto it = iter.begin(); it != iter.end(); ++it, ++count) {
            EXPECT_FALSE(it.IsArray());
        }
        EXPECT_THAT(count, 10);
    }
}

TEST(ObjectFactoryTest, ConcurrentPublish) {
    auto typeInfo = MakeObjectTypeInfo(24);
    ObjectFactory objectFactory;
    constexpr int kThreadCount = kDefaultThreadCount;
    std::atomic<bool> canStart(false);
    std::atomic<int> readyCount(0);
    std::vector<std::thread> threads;
    std::mutex expectedMutex;
    std::vector<ObjHeader*> expected;

    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([&typeInfo, &objectFactory, &canStart, &readyCount, &expected, &expectedMutex]() {
            ObjectFactory::ThreadQueue threadQueue(objectFactory);
            auto* object = threadQueue.CreateObject(typeInfo.get());
            {
                std::lock_guard<std::mutex> guard(expectedMutex);
                expected.push_back(object);
            }
            ++readyCount;
            while (!canStart) {
            }
            threadQueue.Publish();
        });
    }

    while (readyCount < kThreadCount) {
    }
    canStart = true;
    for (auto& t : threads) {
        t.join();
    }

    auto iter = objectFactory.Iter();
    std::vector<ObjHeader*> actual;
    for (auto it = iter.begin(); it != iter.end(); ++it) {
        actual.push_back(it.GetObjHeader());
    }

    EXPECT_THAT(actual, testing::UnorderedElementsAreArray(expected));
}
