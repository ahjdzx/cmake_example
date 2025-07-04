#include "pybind11/pytypes.h"
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/subinterpreter.h>
#include <queue>
#include <thread>
#include <vector>

namespace py = pybind11;

// 全局初始化Python解释器
void init_python() { py::initialize_interpreter(); }

// 批量执行任务结构体
class BatchTask {
public:
  std::string code;
  // py::dict global_vars;
  // py::dict local_vars;
  std::promise<py::object> result_promise;
};

class SubInterpreterPool {
public:
  SubInterpreterPool(int num_interpreters)
      : num_interpreters_(num_interpreters) {
    for (int i = 0; i < num_interpreters_; ++i) {
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
    // py::finalize_interpreter();
  }

  // 提交任务到子解释器（带 globals 和 locals）
  template <typename F> void submit(F func) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    task_queue_.push(func);
    cv_.notify_one();
  }

  std::vector<py::object>
  batch_run_python_code_with_context(const std::vector<std::string> &tasks,
                                     SubInterpreterPool &pool) {

    std::vector<std::future<py::object>> futures;

    for (const auto &t : tasks) {
      auto bt = std::make_shared<BatchTask>();
      bt->code = t;
      futures.push_back(bt->result_promise.get_future());

      submit([bt]() { // 现在lambda是可复制的
        py::gil_scoped_acquire acquire;
        try {
          py::dict l1;
          l1["a"] = 5;
          l1["b"] = 3;
          l1["x"] = 10;
          l1["y"] = 20;
          py::object result = py::eval(bt->code.c_str(), py::globals(), l1);
          bt->result_promise.set_value(result);
        } catch (pybind11::error_already_set &e) {
          std::cerr << "Error in evaluating expression: " << e.what()
                    << std::endl;
          bt->result_promise.set_exception(std::current_exception());
        } catch (const std::exception &e) {
          std::cerr << e.what() << std::endl;
          bt->result_promise.set_exception(std::current_exception());
        }
      });
    }

    std::vector<py::object> results;
    for (auto &future : futures) {
      try {
        py::gil_scoped_acquire acquire;
        results.push_back(future.get());
      } catch (const std::exception &e) {
        std::cerr << "Error in evaluating expression: " << e.what()
                  << std::endl;
        results.push_back(py::none()); // 插入 None 作为错误占位符
      }
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

      // 激活子解释器并执行任务
      {
        // py::gil_scoped_release release; // 释放主线程的GIL
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

// 示例：调用Python函数
void call_python_function() {
  py::object module = py::module_::import("math");
  py::object result = module.attr("sqrt")(16.0);
  std::cout << "Result: " << std::endl;
  py::print(result);
}

int main() {
  init_python();

  // 创建子解释器池（假设使用4个子解释器）
  SubInterpreterPool pool(4);

  // 构建批量任务
  std::vector<std::string> tasks;

  tasks.emplace_back("'Hello ' + 'World'");

  tasks.emplace_back("a + b");

  tasks.emplace_back("x * y");

  tasks.emplace_back("3 ** 2 + 4 ** 2");

  // 批量执行
  std::vector<py::object> results =
      pool.batch_run_python_code_with_context(tasks, pool);

  // 输出结果
  for (size_t i = 0; i < results.size(); ++i) {
    if (results[i].is_none()) {
      std::cout << "Task " << i << ": Error or no result" << std::endl;
    } else {
      std::cout << "Task " << i << " result: " << std::endl;
      py::print(results[i]);
    }
  }

  return 0;
}