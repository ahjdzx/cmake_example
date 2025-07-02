#include <iostream>
#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <vector>

namespace py = pybind11;

int main(int argc, char *argv[]) {
  py::scoped_interpreter guard{};

  // Add current directory to Python's sys.path
  py::module_ sys = py::module_::import("sys");
  sys.attr("path").attr("append")("."); // <-- Add this line

  try {
    py::object sum = py::module_::import("src.demo.sum");
    py::object py_list_sum = sum.attr("py_list_sum");
    int result = py_list_sum(std::vector<int>{1, 2, 3, 4, 5}).cast<int>();
    std::cout << "py_list_sum([1,2,3,4,5]) result: " << result << std::endl;
  } catch (const py::error_already_set &e) {
    std::cerr << "Python error: " << e.what() << ", trace: " << e.trace()
              << std::endl;
    return 1;
  }
  return 0;
}