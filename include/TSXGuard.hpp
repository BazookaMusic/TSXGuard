#ifndef INCLUDE_TSX_GUARD_HPP
    
    #define INCLUDE_TSX_GUARD_HPP

#include <atomic>
#include <vector>

#include "rtm.h"
//#include <intrin.h>
#include "emmintrin.h"
#include "iostream"


// #define _XBEGIN_STARTED		(~0u)
// #define _XABORT_EXPLICIT	(1 << 0)
// #define _XABORT_RETRY		(1 << 1)
// #define _XABORT_CONFLICT	(1 << 2)
// #define _XABORT_CAPACITY	(1 << 3)
// #define _XABORT_DEBUG		(1 << 4)
// #define _XABORT_NESTED		(1 << 5)

namespace TSX {
    static constexpr int ABORT_VALIDATION_FAILURE = 0xee;
    static constexpr int ABORT_GL_TAKEN = 0;

    class SpinLock {
        
        private:
            enum {
                UNLOCKED = false,
                LOCKED = true
            };
            std::atomic<bool> spin_lock;
        public:
            SpinLock(): spin_lock(false) {}

            void lock() noexcept{

                //bool unlocked = UNLOCKED;

                for (;;) {
                    // test
                    while (spin_lock.load(std::memory_order_relaxed) == LOCKED) _mm_pause();

                    if (!spin_lock.exchange(LOCKED)) {
                        break;
                    }

                }
            }

            void unlock() noexcept{
                spin_lock.store(false);
            }

            bool isLocked() noexcept {
                return spin_lock.load(std::memory_order_relaxed);
            }

    };

    enum {
	TX_ABORT_CONFLICT = 0,
	TX_ABORT_CAPACITY,
	TX_ABORT_EXPLICIT,
    TX_ABORT_LOCK_TAKEN,
	TX_ABORT_REST,
	TX_ABORT_REASONS_END
    };

    struct TSXStats {
         int tx_starts,
            tx_commits,
            tx_aborts,
            tx_lacqs;

        int tx_aborts_per_reason[TX_ABORT_REASONS_END];

        TSXStats(): tx_starts(0), tx_commits(0), tx_aborts(0), tx_lacqs(0) {
            for (int i = 0; i < TX_ABORT_REASONS_END; i++) {
                tx_aborts_per_reason[i] = 0;
            }
        }


        void print_stats() {
            std::cout << "Transaction stats:" << std::endl 
            << "Starts:" << tx_starts << std::endl <<
            "Commits:" << tx_commits << std::endl <<
            "Aborts:" << tx_aborts << std::endl <<
            "Lock acquisitions:" << tx_lacqs << std::endl <<
            "Conflict Aborts:" << tx_aborts_per_reason[0] << std::endl <<
            "Capacity Aborts:" << tx_aborts_per_reason[1] << std::endl <<
            "Explicit Aborts:" << tx_aborts_per_reason[2] << std::endl <<
            "Lock Taken Aborts:" << tx_aborts_per_reason[3] << std::endl <<
            "Other Aborts:" << tx_aborts_per_reason[4] << std::endl;

        }


    };


    TSXStats total_stats(std::vector<TSXStats> stats) {
        TSXStats total_stats;

        for (auto i = stats.begin(); i != stats.end(); i++) {
            total_stats.tx_starts += i->tx_starts;
            total_stats.tx_commits += i->tx_commits;
            total_stats.tx_aborts += i->tx_aborts;
            total_stats.tx_lacqs += i->tx_lacqs;
            total_stats.tx_aborts_per_reason[0] += i->tx_aborts_per_reason[0];
            total_stats.tx_aborts_per_reason[1] += i->tx_aborts_per_reason[1];
            total_stats.tx_aborts_per_reason[2] += i->tx_aborts_per_reason[2];
            total_stats.tx_aborts_per_reason[3] += i->tx_aborts_per_reason[3];
            total_stats.tx_aborts_per_reason[4] += i->tx_aborts_per_reason[4];
        }

        return total_stats;
    }

    

    class TSXGuard {
    private:
       
        const int max_retries;
        SpinLock &spin_lock;
        TSXStats &_stats;
        bool has_locked; 

    public:
        TSXGuard(const int max_tx_retries, SpinLock &mutex, TSXStats &stats): 
        max_retries(max_tx_retries),
        spin_lock(mutex),
        _stats(stats),
        has_locked(false)
        {

            int nretries = 0; 

            while(1) {

                ++nretries;
                
                // try to init transaction
                unsigned int status = _xbegin();
                switch (status) {
                    case _XBEGIN_STARTED:   // tx started
                        _stats.tx_starts++;
                        if (!spin_lock.isLocked()) return; //successfully started transaction
                        
                        // started txn but someone is executing the txn  section non-speculatively 
                        // (acquired the  fall-back lock) -> aborting

                        _stats.tx_aborts++;
                        _stats.tx_aborts_per_reason[TX_ABORT_LOCK_TAKEN]++;

                        _xabort(ABORT_GL_TAKEN); // abort with code 0xff  
                        break;
                    case _XABORT_CAPACITY:  // no space in buffers

                        _stats.tx_aborts++;
                        _stats.tx_aborts_per_reason[TX_ABORT_CAPACITY]++;

                        break;
                    case _XABORT_CONFLICT:  // data sharing

                        _stats.tx_aborts++;
                        _stats.tx_aborts_per_reason[TX_ABORT_CONFLICT]++;

                        break;
                    case _XABORT_EXPLICIT:  // somebody explicitly cancelled the transaction
                        _stats.tx_aborts++;

                        if (_XABORT_CODE(status) == ABORT_GL_TAKEN && !(status & _XABORT_NESTED)) {
                            while (spin_lock.isLocked());// _mm_pause();
                        } else if(!(status & _XABORT_RETRY)) {
                            // if the system recommends not to retry
                            // go to the fallback immediately
                            goto fallback_lock; 
                        } else {
                            _stats.tx_aborts_per_reason[TX_ABORT_REST]++;
                        }   
                        break;

                    default:
                        break;
                }
                 
                // too many retries, take the fall-back lock 
                if (nretries >= max_retries) break;

            }   //end
    fallback_lock:
                has_locked = true;
                spin_lock.lock();
        }

        ~TSXGuard() {
            if (has_locked && spin_lock.isLocked()) {
                _stats.tx_lacqs++;
                _stats.tx_aborts_per_reason[TX_ABORT_LOCK_TAKEN]++;
                spin_lock.unlock();
            } else {
                _stats.tx_commits++;
                _xend();
            }
        }     
    };

};



#endif
