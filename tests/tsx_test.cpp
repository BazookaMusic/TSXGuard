
#include <chrono>
#include <sstream>
#include <iostream>
#include <ctime>
#include <thread>
#include <vector>

#include "../include/catch.hpp"

#include "../include/TSXGuard.hpp"

static const int THREADS = 4;


void increment(bool &noEntryFlag, int &data, TSX::SpinLock &spin_lock) {
    
    spin_lock.lock();

    noEntryFlag = true;

    if (noEntryFlag) {
        data++;
    } 

    noEntryFlag = false;

    spin_lock.unlock();
}

TEST_CASE("SpinLockTest TEST", "[lock]") {
    TSX::SpinLock spin_lock;

    std::thread threads[THREADS];
    bool noEntryFlag = false;
    int counter = 0;
    SECTION("Testing lock") {

        for (int j = 0; j < 100; j++) {

            noEntryFlag = false;
            counter = 0;

            for (int i = 0; i < THREADS; i++) {
            threads[i % THREADS] =
                std::thread(increment, std::ref(noEntryFlag),std::ref(counter), std::ref(spin_lock));
            }

            for (int i = 0; i < THREADS; i++) {
                threads[i].join();
            }

            if (counter == THREADS) {
                REQUIRE(counter == THREADS);
            } else {
                std::cerr << "ERROR: Synchronization error, threads should have counter "
                << THREADS << "but got " << counter << " instead." << std::endl;
                REQUIRE(counter == THREADS);
            }

        }   
        
    }
}


void transactional_increment(bool &noEntryFlag, int *data, TSX::SpinLock &spin_lock, TSX::TSXStats &stats) {
    TSX::TSXGuard guard(20,spin_lock,stats);
    
    
    (void)noEntryFlag;
    //noEntryFlag = true;

    for (int i = 0; i < 1000000; i++) {
        i = i | 0;
    }

    if (!noEntryFlag) {
        for (int i = 0; i < THREADS; i++) {
            data[i]++;
        }
    }

    
}


TEST_CASE("TSX RTM TEST", "[tsx]") {
    TSX::SpinLock spin_lock;

    int counter[THREADS];
    bool noEntryFlag = false;

    std::thread threads[THREADS];

    std::vector<TSX::TSXStats> stats(THREADS);

    for (int j=0; j < 100; j++) {

        for (int k = 0; k < THREADS; k++) {
            counter[k] = 0;
        }

        noEntryFlag = false;

        for (int i = 0; i < THREADS; i++) {
            threads[i % THREADS] = std::thread(transactional_increment, std::ref(noEntryFlag), 
            std::ref(counter), std::ref(spin_lock), std::ref(stats[i]));
        }

        for (int i = 0; i < THREADS; i++) {
                threads[i].join();
        }

        for (int k = 0; k < THREADS; k++) {
            if (counter[k] == THREADS) {
                REQUIRE(counter[k] == THREADS);
            } else {
                std::cout << counter[k] << std::endl;
                REQUIRE(counter[k] == THREADS);
            }
        }
        

    }

    TSX::total_stats(stats).print_stats();
}



