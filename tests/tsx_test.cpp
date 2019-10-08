
#include <chrono>
#include <sstream>
#include <iostream>
#include <ctime>
#include <thread>
#include <vector>


#include "../include/catch.hpp"

#include "../include/TSXGuard.hpp"

#include "../include/rtm.h"

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
    unsigned char status = 0;
    TSX::TSXGuardWithStats guard(20,spin_lock,status,stats);
    
    noEntryFlag = true;

    for (int i = 0; i < 1000000; i++) {
        i = i | 0;
    }

    if (noEntryFlag) {
        for (int i = 0; i < THREADS; i++) {
            data[i]++;
        }
    }

    noEntryFlag = false;
    
}


TEST_CASE("TSX RTM TEST", "[tsx]") {
    std::cout << "Testing RTM Implementation" << std::endl;
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
            threads[i] = std::thread(transactional_increment, std::ref(noEntryFlag), 
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

    std::cout << "Displaying Statistics:" << std::endl;
    TSX::total_stats(stats).print_stats();
}

bool transactional_abort(TSX::SpinLock &spin_lock) {
     std::cout << "Testing RTM Abort Implementation" << std::endl;
    unsigned char status = 0;
    while(status != 3) {
        status = 0; 
        TSX::TSXGuard guard(20,spin_lock, status);

        guard.abort<3>();
    }

    if (status != 3) {
        std::cerr << "Failed simple abort. Status not updated!" << std::endl;
        return false;
    }

    status = 0;
    int retries = 20;
    while(1) {
        TSX::TSXGuard guard(retries,spin_lock, status);
        if (retries > 0) {
            retries = guard.abort_to_retry<3>();
        } else {
            break;
        }
    }

    return status == 3;

    
    
}

TEST_CASE("TSX RTM ABORT TEST", "[tsx]") {
    TSX::SpinLock spin_lock;
    REQUIRE(transactional_abort(spin_lock));
}



