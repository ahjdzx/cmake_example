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
#include <unordered_map>
#include <vector>

namespace py = pybind11;

// 全局初始化Python解释器
void init_python() { py::initialize_interpreter(); }

// 批量执行任务结构体
class BatchTask {
public:
  std::string code;
  std::unordered_map<std::string, int> local_ints; // 使用基本类型存储变量
  std::promise<std::string> result_promise;        // 返回字符串结果
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
  }

  template <typename F> void submit(F func) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    task_queue_.push(func);
    cv_.notify_one();
  }

  std::vector<std::string> batch_run_python_code_with_context(
      const std::vector<std::string> &tasks,
      const std::unordered_map<std::string, int> &shared_locals) {
    std::vector<std::future<std::string>> futures;

    for (const auto &t : tasks) {
      auto bt = std::make_shared<BatchTask>();
      bt->code = t;
      bt->local_ints = shared_locals; // 复制基本类型数据
      futures.push_back(bt->result_promise.get_future());

      submit([bt]() {
        try {
          // 在子解释器内创建局部变量
          py::dict locals;
          for (const auto &kv : bt->local_ints) {
            locals[kv.first.c_str()] = kv.second;
          }

          // 执行Python代码
          py::object result = py::eval(bt->code.c_str(), py::globals(), locals);

          // 将结果转为字符串（避免跨解释器对象）
          bt->result_promise.set_value(py::str(result).cast<std::string>());
        } catch (pybind11::error_already_set &e) {
          std::cerr << "Python error: " << e.what() << std::endl;
          bt->result_promise.set_value("ERROR: " + std::string(e.what()));
        } catch (const std::exception &e) {
          std::cerr << "System error: " << e.what() << std::endl;
          bt->result_promise.set_value("ERROR: " + std::string(e.what()));
        }
      });
    }

    std::vector<std::string> results;
    for (auto &future : futures) {
      results.push_back(future.get());
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
        py::subinterpreter_scoped_activate activate(*sub);
        py::gil_scoped_acquire acquire; // 确保获取GIL
        try {
          task();
        } catch (const std::exception &e) {
          std::cerr << "Task execution error: " << e.what() << std::endl;
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

int main() {
  init_python();

  // 创建子解释器池
  SubInterpreterPool pool(4);

  // 构建批量任务
  std::vector<std::string> tasks = {
      "'Hello ' + 'World'", // 任务1: 字符串操作
      "a + b",              // 任务2: 使用变量a,b
      "x * y",              // 任务3: 使用变量x,y
      "3 ** 2 + 4 ** 2"     // 任务4: 数学运算
  };

  // 使用基本类型存储变量
  std::unordered_map<std::string, int> shared_locals = {
      {"a", 5},
      {"b", 3},
      {"x", 10},
      {"y", 20},
  };

  // 批量执行
  std::vector<std::string> results =
      pool.batch_run_python_code_with_context(tasks, shared_locals);

  // 输出结果
  for (size_t i = 0; i < results.size(); ++i) {
    std::cout << "Task " << i << " result: " << results[i] << std::endl;
  }

  return 0;
}