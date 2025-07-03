#include <iostream>
#include <pybind11/embed.h>
#include <pybind11/subinterpreter.h>

namespace py = pybind11;

PYBIND11_EMBEDDED_MODULE(printer, m,
                         py::multiple_interpreters::per_interpreter_gil()) {
  m.def("which", [](const std::string &when) {
    std::cout << when << "; Current Interpreter is "
              << py::subinterpreter::current().id() << std::endl;
  });
}

/**
 * @brief 演示了Python子解释器的创建、激活和切换操作
 *
 * 该代码展示了如何使用pybind11进行以下操作：
 * 1. 创建主Python解释器环境
 * 2. 创建子解释器并在其中执行Python代码
 * 3. 在子解释器和主解释器之间切换
 * 4. 处理Python异常
 * 5. 使用GIL锁管理线程安全
 *
 * 代码通过调用名为"printer"的Python模块的"which"函数来跟踪当前活动的解释器。
 * 包含了嵌套的作用域和解释器切换的复杂场景演示。
 * 参考: https://pybind11.readthedocs.io/en/latest/advanced/embedding.html#full-sub-interpreter-example
 */
int main() {
  // 创建主Python解释器环境
  py::scoped_interpreter main_interp;

  py::module_::import("printer").attr("which")("First init");

  {
    py::subinterpreter sub = py::subinterpreter::create();

    py::module_::import("printer").attr("which")("Created sub");

    {
      py::subinterpreter_scoped_activate guard(sub);
      try {
        py::module_::import("printer").attr("which")("Activated sub");
      } catch (py::error_already_set &e) {
        std::cerr << "EXCEPTION " << e.what() << std::endl;
        return 1;
      }
    }

    py::module_::import("printer").attr("which")("Deactivated sub");

    {
      py::gil_scoped_release nogil;
      {
        py::subinterpreter_scoped_activate guard(sub);
        try {
          {
            py::subinterpreter_scoped_activate main_guard(
                py::subinterpreter::main());
            try {
              py::module_::import("printer").attr("which")("Main within sub");
            } catch (py::error_already_set &e) {
              std::cerr << "EXCEPTION " << e.what() << std::endl;
              return 1;
            }
          }
          py::module_::import("printer").attr("which")(
              "After Main, still within sub");
        } catch (py::error_already_set &e) {
          std::cerr << "EXCEPTION " << e.what() << std::endl;
          return 1;
        }
      }
    }
  }

  py::module_::import("printer").attr("which")("At end");

  return 0;
}