// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#ifndef LC_PROJECTS_TSUBAKI_WORKER_HPP
#define LC_PROJECTS_TSUBAKI_WORKER_HPP

#include "basic_taskstream.hpp"
#include "checksum.hpp"

#include <atomic>
#include <ostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace tsubaki
{
class work_result
{
  public:
    algorithm alg = algorithm::error;
    std::vector<error_entry> errors;
    unsigned success;
    unsigned failed;
    unsigned unprocessed;
    double duration;

    work_result() = default;
    work_result(algorithm _alg, const std::vector<error_entry> &_errors,
                unsigned _success, unsigned _failed, unsigned _unprocessed,
                double _duration)
        : alg(_alg), errors(_errors), success(_success), failed(_failed),
          unprocessed(_unprocessed), duration(_duration)
    {
    }
    work_result operator+(const work_result &target)
    {
        if (alg != target.alg)
            throw std::invalid_argument(
                "tsubaki::work_result::operator+: bad usage: cannot add work "
                "results of different algorithms");
        work_result sum = *this;
        sum.errors.insert(sum.errors.end(), target.errors.begin(),
                          target.errors.end());
        sum.success += target.success;
        sum.failed += target.failed;
        sum.unprocessed += target.unprocessed;
        sum.duration += target.duration;
        return sum;
    }
};

inline std::ostream &operator<<(std::ostream &os, const work_result &wr)
{
    os << "# ----------General Report----------\n";
    os << "# Algorithm: " << alg_to_str(wr.alg) << std::endl;
    os << "# Total Processed: " << wr.success + wr.failed << std::endl;
    os << "# Succeeded: " << wr.success << std::endl;
    os << "# Failed: " << wr.failed << std::endl;
    os << "# Unprocessed: " << wr.unprocessed << std::endl;
    os << "# Duration: " << std::chrono::duration<double>(wr.duration).count()
       << " seconds" << std::endl;
    if (wr.errors.empty())
        return os;
    os << "#\n# Error messages: " << std::endl;
    for (auto &it : wr.errors)
    {
        os << "# Value= " << it.ec.value() << ", Message= " << it.ec.message()
           << ", Operation= " << it.op << std::endl;
    }
    return os;
}

class work_manager
{
  private:
    std::ostream &os;
    basic_taskstream &tasks;
    std::vector<std::thread> workers;
    std::thread progress;
    std::atomic<algorithm> alg;
    std::atomic<unsigned> success = 0;
    std::atomic<unsigned> failed = 0;
    std::atomic<unsigned> unprocessed = 0;
    bool quiet;
    bool catch_sigint;
    void worker();
    void printer();
    void wait();

  public:
    work_manager(basic_taskstream &_tasks, std::ostream &_os, algorithm _alg,
                 bool verbose = false, bool _catch_sigint = true)
        : os(_os), tasks(_tasks), quiet(!verbose), alg(_alg),
          catch_sigint(_catch_sigint)
    {
    }
    work_manager() = delete;
    work_manager(const work_manager &) = delete;
    work_manager &operator=(const work_manager &) = delete;
    work_result run(unsigned thread_count);
};

} // namespace tsubaki

#endif
