#include "proxnlp/python/function.hpp"
#include "proxnlp/function-ops.hpp"

namespace proxnlp {
namespace python {

void exposeFunctionTypes() {
  using context::C1Function;
  using context::C2Function;
  using context::ConstVectorRef;
  using context::Function;

  bp::class_<FunctionWrap, boost::noncopyable>(
      "BaseFunction", "Base class for functions.",
      bp::init<int, int, int>(bp::args("self", "nx", "ndx", "nr")))
      .def("__call__", bp::pure_virtual(&Function::operator()),
           bp::args("self", "x"), "Call the function.")
      .add_property("nx", &Function::nx, "Input dimension")
      .add_property("ndx", &Function::ndx, "Input tangent space dimension.")
      .add_property("nr", &Function::nr, "Function codimension.");

  context::MatFuncType C1Function::*compJac1 = &C1Function::computeJacobian;
  context::MatFuncRetType C1Function::*compJac2 = &C1Function::computeJacobian;

  bp::class_<C1FunctionWrap, bp::bases<Function>, boost::noncopyable>(
      "C1Function", "Base class for differentiable functions",
      bp::init<int, int, int>(bp::args("self", "nx", "ndx", "nr")))
      .def("computeJacobian", bp::pure_virtual(compJac1),
           bp::args("self", "x", "Jout"))
      .def("getJacobian", compJac2, bp::args("self", "x"),
           "Compute and return Jacobian.");

  bp::class_<C2FunctionWrap, bp::bases<C1Function>, boost::noncopyable>(
      "C2Function", "Base class for twice-differentiable functions.",
      bp::init<int, int, int>(bp::args("self", "nx", "ndx", "nr")))
      .def("vectorHessianProduct", &C2Function::vectorHessianProduct,
           &C2FunctionWrap::default_vhp, bp::args("self", "x", "v", "Hout"))
      .def("getVHP", &C2FunctionWrap::getVHP, bp::args("self", "x", "v"),
           "Compute and return the vector-Hessian product.");

  bp::class_<ComposeFunctionTpl<context::Scalar>, bp::bases<C2Function>>(
      "ComposeFunction", "Composition of two functions.",
      bp::init<const shared_ptr<C2Function> &, const shared_ptr<C2Function> &>(
          bp::args("self", "left", "right")));

  bp::def("compose", &::proxnlp::compose<context::Scalar>,
          bp::args("fn1", "fn2"),
          "Returns the composition of two C2Function objects.");
}

} // namespace python
} // namespace proxnlp
