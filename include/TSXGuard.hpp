#ifndef INCLUDE_TSX_GUARD_HPP
    
    #define INCLUDE_TSX_GUARD_HPP

#include <atomic>
#include <vector>
#include "rtm.h"
#include "emmintrin.h"
#include "iostream"

namespace TSX {
    static constexpr int ALIGNMENT = 128;
    static constexpr int ABORT_VALIDATION_FAILURE = 0xee;
    static constexpr int ABORT_GL_TAKEN = 0;
    static constexpr int USER_OPTION_LOWER_BOUND = 0x01;

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

    struct alignas(ALIGNMENT) TSXStats {
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


    // TSXGuard works similarly to std::lock_guard
    // but uses hardware transactional memory to 
    // achieve synchronization. The result is the
    // synchronization of commands in all TSXGuards'
    // scopes.
    class TSXGuard {
    protected:
       
        const int max_retries;  // how many retries before lock acquire
        SpinLock &spin_lock;    // fallback
        bool has_locked;        // avoid checking global lock if haven't locked
        bool user_explicitly_aborted;   // explicit user aborts mean that lock is not taken and
                                        // transaction not pending
        int nretries;           // how many retries have been made so far
                                // used to resume transaction in case of user abort
    public:
        TSXGuard(const int max_tx_retries, SpinLock &mutex, unsigned char &err_status): 
        max_retries(max_tx_retries),
        spin_lock(mutex),
        has_locked(false),
        user_explicitly_aborted(false),
        nretries(0)
        {
            while(1) {

                ++nretries;
                
                // try to init transaction
                unsigned int status = _xbegin();
                if (status == _XBEGIN_STARTED) {      // tx started
                    if (!spin_lock.isLocked()) return; //successfully started transaction
                    // started txn but someone is executing the txn  section non-speculatively
                    // (acquired the  fall-back lock) -> aborting
                    _xabort(ABORT_GL_TAKEN); // abort with code 0xff  
                } else if (status & _XABORT_EXPLICIT) {
                    if (_XABORT_CODE(status) == ABORT_GL_TAKEN && !(status & _XABORT_NESTED)) {
                        while (spin_lock.isLocked()) _mm_pause();
                    } else if (_XABORT_CODE(status) > USER_OPTION_LOWER_BOUND) {
                        user_explicitly_aborted = true;
                        err_status = _XABORT_CODE(status);
                        return;
                    } else if(!(status & _XABORT_RETRY)) {
                        // if the system recommends not to retry
                        // go to the fallback immediately
                        goto fallback_lock; 
                    } 
                } 

                // too many retries, take the fall-back lock 
                if (nretries >= max_retries) break;

            }   //end
    fallback_lock:
                has_locked = true;
                spin_lock.lock();
        }

        // abort_to_retry: aborts current transaction
        // and returns retries left
        // in order to retry transaction.
        // Takes the error code as a template
        // parameter.
        template <unsigned char imm>
        int abort_to_retry() {
            static_assert(imm > USER_OPTION_LOWER_BOUND, 
            "User aborts should be larger than USER_OPTION_LOWER_BOUND, as lower numbers are reserved");
            _xabort(imm);
            user_explicitly_aborted = true;
            return max_retries - nretries;
        }

        // abort_to_retry: aborts current transaction.
        // Takes the error code as a template
        // parameter.
        template <unsigned char imm>
        void abort() {
            static_assert(imm > USER_OPTION_LOWER_BOUND, 
            "User aborts should be larger than USER_OPTION_LOWER_BOUND, as lower numbers are reserved");
            _xabort(imm);
            user_explicitly_aborted = true;
        }


        ~TSXGuard() {
            if (!user_explicitly_aborted) {
                // no abort code
                if (has_locked && spin_lock.isLocked()) {
                    spin_lock.unlock();
                } else {
                    _xend();
                }
            }
            
        }     
    };


    class TSXGuardWithStats {
    private:
        const int max_retries;  // how many retries before lock acquire
        SpinLock &spin_lock;    // fallback
        bool has_locked;        // avoid checking global lock if haven't locked
        bool user_explicitly_aborted;   // explicit user aborts mean that lock is not taken and
                                        // transaction not pending
        int nretries;           // how many retries have been made so far
                                // used to resume transaction in case of user abort
        TSXStats &_stats;
    public:
        TSXGuardWithStats(const int max_tx_retries, SpinLock &mutex, unsigned char &err_status, TSXStats &stats):
        max_retries(max_tx_retries),
        spin_lock(mutex),
        has_locked(false),
        user_explicitly_aborted(false),
        nretries(0),
        _stats(stats)
        {
            while(1) {


                ++nretries;
                
                // try to init transaction
                unsigned int status = _xbegin();
                if (status == _XBEGIN_STARTED) {   // tx started
                    _stats.tx_starts++;
                    if (!spin_lock.isLocked()) return;  //successfully started transaction
                    
                    // started txn but someone is executing the txn  section non-speculatively 
                    // (acquired the  fall-back lock) -> aborting

                    _xabort(ABORT_GL_TAKEN); // abort with code 0xff  
                } else if (status & _XABORT_CAPACITY) {
                    _stats.tx_aborts++;
                    _stats.tx_aborts_per_reason[TX_ABORT_CAPACITY]++;
                } else if (status & _XABORT_CONFLICT) {
                    _stats.tx_aborts++;
                    _stats.tx_aborts_per_reason[TX_ABORT_CONFLICT]++;
                } else if (status & _XABORT_EXPLICIT) {
                     _stats.tx_aborts++;
                     _stats.tx_aborts_per_reason[TX_ABORT_EXPLICIT]++;
                    if (_XABORT_CODE(status) == ABORT_GL_TAKEN && !(status & _XABORT_NESTED)) {
                        _stats.tx_aborts_per_reason[TX_ABORT_LOCK_TAKEN]++;
                        while (spin_lock.isLocked());// _mm_pause();
                    } else if (_XABORT_CODE(status) > USER_OPTION_LOWER_BOUND) {
                        user_explicitly_aborted = true;
                        _stats.tx_aborts_per_reason[TX_ABORT_LOCK_TAKEN]++;
                        err_status = _XABORT_CODE(status);
                        return;
                    } else if(!(status & _XABORT_RETRY)) {
                        _stats.tx_aborts_per_reason[TX_ABORT_REST]++;
                        // if the system recommends not to retry
                        // go to the fallback immediately
                        goto fallback_lock; 
                    } else {
                        _stats.tx_aborts_per_reason[TX_ABORT_REST]++;
                    }   
                }
                
                 
                // too many retries, take the fall-back lock 
                if (nretries >= max_retries) break;

            }   //end
    fallback_lock:
                _stats.tx_lacqs++;
                has_locked = true;
                spin_lock.lock();
        }

        int getRemainingRetries() const {
            return max_retries - nretries;
        }

        ~TSXGuardWithStats() {
            if (!user_explicitly_aborted) {
                // no abort code
                if (has_locked && spin_lock.isLocked()) {
                    spin_lock.unlock();
                } else {
                    _stats.tx_commits++;
                    _xend();
                }
            }
            
        }     
        
    };


};


    


#endif
