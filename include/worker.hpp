// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 Lawrence Charland

#ifndef LCPROJECTS_TSUBAKI_WORKER_HPP
#define LCPROJECTS_TSUBAKI_WORKER_HPP

#include "basic_taskstream.hpp"
#include "checksum.hpp"

#include <atomic>
#include <mutex>
#include <ostream>
#include <thread>
#include <vector>

namespace tsubaki
{
class work_result
{
  public:
    algorithm alg = algorithm::error;
    bool verify = false;
    std::vector<error_entry> errors;
    std::vector<task_token> changed;
    std::vector<task_token> unsure;

    unsigned success = 0;
    unsigned failed = 0;
    unsigned unprocessed = 0;
    bool interrupted = false;
    double duration = 0;

    work_result() = default;
    work_result(algorithm _alg, bool _verify,
                const std::vector<error_entry> &_errors,
                const std::vector<task_token> &_changed,
                const std::vector<task_token> &_unsure, unsigned _success,
                unsigned _failed, unsigned _unprocessed, double _duration,
                bool _int)
        : alg(_alg), verify(_verify), errors(_errors), changed(_changed),
          unsure(_unsure), success(_success), failed(_failed),
          unprocessed(_unprocessed), duration(_duration), interrupted(_int)
    {
    }
    void print(std::ostream &os, bool in_details = true);
};

class work_manager
{
  private:
    std::ostream &os;
    basic_taskstream &tasks;
    std::vector<std::thread> workers;
    std::thread progress;
    const checksum_api api;
    std::atomic<bool> interrupted = false;
    std::atomic<unsigned> success = 0;
    std::atomic<unsigned> failed = 0;
    std::atomic<unsigned> unprocessed = 0;
    std::vector<task_token> changed;
    std::vector<task_token> unsure;

    bool verify;
    bool quiet;
    bool catch_sigint;
    void worker(bool _verify);
    void printer();
    void wait();
    mutable std::mutex _mutex;

  public:
    work_manager(basic_taskstream &_tasks, std::ostream &_os,
                 const checksum_api &_api, bool _verify, bool verbose,
                 bool _catch_sigint)
        : os(_os), tasks(_tasks), verify(_verify), quiet(!verbose), api(_api),
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
