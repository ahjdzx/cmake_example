#include "pybind11/pytypes.h"
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <pybind11/detail/common.h>
#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>
#include <pybind11/subinterpreter.h>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace py = pybind11;

class SubInterpreterPool {
public:
  SubInterpreterPool(int num_interpreters)
      : num_interpreters_(num_interpreters) {

    for (int i = 0; i < num_interpreters_; ++i) {
      // 使用unique_ptr管理subinterpreter
      interpreters_.push_back(
          std::make_unique<py::subinterpreter>(py::subinterpreter::create()));
      threads_.emplace_back(&SubInterpreterPool::worker_thread, this,
                            interpreters_.back().get());
    }
  }

  ~SubInterpreterPool() {
    stop_ = true;
    cv_.notify_all();
    for (auto &thread : threads_) {
      thread.join();
    }
    interpreters_.clear();
  }

  template <typename F> void submit(F func) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    task_queue_.push(func);
    cv_.notify_one();
  }

  // 分批提交 Python 表达式，并返回每个表达式的结果 future 列表
  std::vector<std::string>
  submit_batch(const std::vector<std::string> &expressions,
               const std::unordered_map<std::string, int> shared_locals) {

    std::vector<std::future<std::string>> futures;

    for (const auto &code : expressions) {
      auto task = std::make_shared<std::packaged_task<std::string()>>(
          [code, shared_locals]() {
            // 在子解释器内创建局部变量
            py::dict locals;
            for (const auto &kv : shared_locals) {
              locals[kv.first.c_str()] = kv.second;
            }
            return py::eval(code.c_str(), py::globals(),
                            locals); // 使用 eval 获取返回值
          });

      futures.push_back(task->get_future());
      submit([task]() { (*task)(); });
    }

    std::vector<std::string> results;
    for (auto &future : futures) {
      try {
        py::gil_scoped_acquire acquire;
        std::string result = future.get();
        results.push_back(result);
      } catch (const std::future_error &e) {
        std::cerr << "Future error: " << e.what() << std::endl;
        // results.push_back(py::str());
      } catch (const py::error_already_set &e) {
        std::cerr << "Python error: " << e.what() << std::endl;
        // results.push_back(py::str());
      }
    }
    return results;
  }

  // 批量提交优化版本
  std::vector<std::string> submit_batch_optimized(
      const std::vector<std::string> &expressions,
      const std::unordered_map<std::string, int> shared_locals) {
    const size_t batch_size = expressions.size();
    std::vector<std::future<std::string>> futures;
    futures.reserve(batch_size);

    // 1. 批量提交优化：单次锁保护整个任务提交
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      for (const auto &code : expressions) {
        auto task = std::make_shared<std::packaged_task<std::string()>>(
            [this, code, shared_locals]() {
              // 确保子解释器线程持有自己的GIL
              py::gil_scoped_acquire acquire;
              try {
                // 在子解释器内创建局部变量
                py::dict locals;
                for (const auto &kv : shared_locals) {
                  locals[kv.first.c_str()] = kv.second;
                }

                // 使用子解释器独立的全局上下文
                py::object result = py::eval(code, py::globals(), locals);
                return py::str(result).cast<std::string>();
              } catch (py::error_already_set &e) {
                // 保留异常信息并传递
                return py::cast(e).cast<std::string>();
              } catch (...) {
                return py::cast(std::current_exception()).cast<std::string>();
              }
            });

        futures.emplace_back(task->get_future());
        task_queue_.push([task]() { (*task)(); });
      }
      // 单次通知所有工作线程
      cv_.notify_all();
    }

    // 2. 异步结果收集
    std::vector<std::string> results;
    results.reserve(batch_size);

    // 使用等待策略避免顺序阻塞
    size_t completed = 0;
    while (completed < batch_size) {
      for (auto &future : futures) {
        if (future.valid() && future.wait_for(std::chrono::seconds(0)) ==
                                  std::future_status::ready) {
          try {
            // 主线程处理结果时需持有GIL
            py::gil_scoped_acquire acquire;
            std::string result = future.get();
            results.emplace_back(std::move(result));
          } catch (const std::exception &e) {
            results.emplace_back(e.what());
          }
          completed++;
          future = {}; // 标记为已处理
        }
      }
      // 避免忙等待
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    return results;
  }

private:
  void worker_thread(py::subinterpreter *sub) {
    while (true) {
      std::function<void()> task;
      {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        cv_.wait(lock, [this] { return stop_ || !task_queue_.empty(); });
        if (stop_ && task_queue_.empty())
          return;
        task = std::move(task_queue_.front());
        task_queue_.pop();
      }

      {
        py::subinterpreter_scoped_activate activate(*sub);
        try {
          task();
        } catch (const std::exception &e) {
          std::cerr << "Python error: " << e.what() << std::endl;
        }
      }
    }
  }

  int num_interpreters_;
  std::vector<std::unique_ptr<py::subinterpreter>> interpreters_;
  std::vector<std::thread> threads_;
  std::queue<std::function<void()>> task_queue_;
  std::mutex queue_mutex_;
  std::condition_variable cv_;
  bool stop_ = false;
};

void run_python_code(const std::string &code) { py::exec(code.c_str()); }

void call_python_function() {
  py::object module = py::module_::import("math");
  py::object result_obj = module.attr("sqrt")(16.0);
  try {
    double result = py::cast<double>(result_obj);
    std::cout << "Result: " << result << std::endl;
  } catch (const py::cast_error &e) {
    std::cerr << "Conversion error: " << e.what() << std::endl;
  }
}

void test1(SubInterpreterPool &pool) {

  pool.submit([] { run_python_code("print('Hello from subinterpreter 1')"); });
  pool.submit([] { call_python_function(); });
  pool.submit([] { run_python_code("x = 5 + 3; print(x)"); });
  pool.submit([] { run_python_code("for i in range(3): print(i)"); });
}

void test2(SubInterpreterPool &pool) {

  std::vector<std::string> tasks = {
      "2 + 3", "5 * 7", "sum([1, 2, 3])", "'hello' + 'world'", "a + b",
  };

  std::cout << "Submitting batch..." << std::endl;

  // 使用基本类型存储变量
  std::unordered_map<std::string, int> shared_locals = {
      {"a", 5},
      {"b", 3},
      {"x", 10},
      {"y", 20},
  };
  auto results = pool.submit_batch(tasks, shared_locals);
  // auto results = pool.submit_batch_optimized(tasks, shared_locals);

  std::cout << "Waiting for results..." << std::endl;
  for (size_t i = 0; i < results.size(); ++i) {
    try {
      std::string result = results[i];
      // std::string result_str = py::str(result).cast<std::string>();
      std::cout << "Result[" << i << "] = " << result << std::endl;
    } catch (const py::error_already_set &e) {
      std::cerr << "Python error in task " << i << ": " << e.what()
                << std::endl;
    } catch (const std::exception &e) {
      std::cerr << "C++ error in task " << i << ": " << e.what() << std::endl;
    }
  }

  std::cout << "All tasks completed." << std::endl;
}

int main() {
  py::scoped_interpreter guard{}; // Manages main interpreter lifecycle

  SubInterpreterPool pool(4);

  // test1(pool);
  test2(pool);
  return 0;
}
