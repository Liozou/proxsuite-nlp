#include "lienlp/python/fwd.hpp"
#include "lienlp/problem-base.hpp"


#include <boost/python/suite/indexing/vector_indexing_suite.hpp>


namespace lienlp
{
  namespace python
  {
    
    void exposeProblem()
    {
      using context::Problem;
      using ConstraintPtr = shared_ptr<context::Constraint>;
      bp::class_<Problem, shared_ptr<Problem>>(
        "Problem", "Problem definition class.",
        bp::init<const context::Cost&,
                 const std::vector<ConstraintPtr>&
                 >(bp::args("cost", "constraints"))
      )
        .def(bp::init<const context::Cost&>(bp::args("cost")))
        .add_property("num_constraint_blocks", &Problem::getNumConstraints, "Get the number of constraint blocks.")
        .add_property("total_constraint_dim", &Problem::getTotalConstraintDim, "Get the total dimension of the constraints.")
        .add_property("constraint_dims", &Problem::getConstraintDims, "Get the dimensions of the constraint blocks.")
        .def("add_constraint", &Problem::addConstraint, bp::args("cstr"),
             "Add a constraint to the problem.")
        ;

    }

  } // namespace python
} // namespace lienlp

