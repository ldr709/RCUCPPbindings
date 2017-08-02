#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <tuple>
#include <utility>

// Derived-type approach.  All RCU-protected data structures using this
// approach must derive from std::rcu_obj_base, which in turn derives
// from std::rcu_head.  No idea what happens in case of multiple inheritance.

namespace std {
    template<typename T, typename D = default_delete<T>>
    class rcu_obj_base: private rcu_head, private std::tuple<D> {
    public:
        void retire(D d = {})
        {
            std::get<0>(*this) = std::move(d);
            ::call_rcu(
                static_cast<rcu_head *>(this),
                [](rcu_head *rhp) {
                    auto rhdp = static_cast<rcu_obj_base *>(rhp);
                    auto obj = static_cast<T *>(rhdp);
                    std::get<0>(*rhdp)(obj);
                }
            );
        }
    };

    // RAII for RCU readers
    class rcu_reader {
    public:
        rcu_reader() noexcept
        {
            rcu_read_lock();
            active = true;
        }
        rcu_reader(std::defer_lock_t) noexcept
        {
            active = false;
        }
        rcu_reader(const rcu_reader &) = delete;
        rcu_reader(rcu_reader&& other) noexcept
        {
            active = other.active;
            other.active = false;
        }
        rcu_reader& operator=(const rcu_reader&) = delete;
        rcu_reader& operator=(rcu_reader&& other) noexcept
        {
            if (active) {
                rcu_read_unlock();
            }
            active = other.active;
            other.active = false;
            return *this;
        }

        ~rcu_reader() noexcept
        {
            if (active) {
                rcu_read_unlock();
            }
        }

        void swap(rcu_reader& other) noexcept
        {
            std::swap(active, other.active);
        }

        void lock() noexcept
        {
            rcu_read_lock();
            active = true;
        }

        void unlock() noexcept
        {
            rcu_read_unlock();
            active = false;
        }

    private:
        bool active;
    };

    void swap(rcu_reader& a, rcu_reader& b) noexcept
    {
        a.swap(b);
    }

    // Methods for RCU updaters
    class rcu_updater {
    public:
    	static void synchronize() noexcept
	{
	    synchronize_rcu();
	}

        static void barrier() noexcept
        {
            rcu_barrier();
        }
    };

    template<typename T, typename D = default_delete<T>>
    void rcu_retire(std::unique_ptr<T, D> ptr)
    {
        typedef std::unique_ptr<T, D> ptr_type;
        struct rcu_obj_base_ni :
            public rcu_obj_base<rcu_obj_base_ni>,
            public ptr_type {
            rcu_obj_base_ni(ptr_type p) : ptr_type(std::move(p)) {}
        };
        (new rcu_obj_base_ni(std::move(ptr)))->retire();
    }

    template<typename T, typename D = default_delete<T>>
    void rcu_retire(T *p, D d = {})
    {
        typedef std::unique_ptr<T, D> ptr_type;
        rcu_retire(ptr_type(p, std::move(d)));
    }

} // namespace std
