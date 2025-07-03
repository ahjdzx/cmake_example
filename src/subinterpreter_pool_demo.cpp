#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory> // 添加头文件
#include <mutex>
#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <pybind11/subinterpreter.h>
#include <queue>
#include <thread>
#include <vector>

namespace py = pybind11;

class SubInterpreterPool {
public:
  SubInterpreterPool(int num_interpreters)
      : num_interpreters_(num_interpreters) {
    
    interpreters_.reserve(num_interpreters);
    threads_.reserve(num_interpreters_);

    for (int i = 0; i < num_interpreters_; ++i) {
      // 使用unique_ptr管理subinterpreter
      interpreters_.emplace_back(
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
        // py::gil_scoped_release release;
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
  std::vector<std::unique_ptr<py::subinterpreter>>
      interpreters_; // 改为存储unique_ptr
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

int main() {
  py::scoped_interpreter guard{}; // Manages main interpreter lifecycle

  SubInterpreterPool pool(4);

  pool.submit([] { run_python_code("print('Hello from subinterpreter 1')"); });
  pool.submit([] { call_python_function(); });
  pool.submit([] { run_python_code("x = 5 + 3; print(x)"); });
  pool.submit([] { run_python_code("for i in range(3): print(i)"); });

  return 0;
}
