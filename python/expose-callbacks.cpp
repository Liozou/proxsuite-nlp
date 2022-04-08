#include "lienlp/python/fwd.hpp"
#include "lienlp/helpers-base.hpp"
#include "lienlp/helpers/history-callback.hpp"

namespace lienlp
{
  namespace python
  {

    struct CallbackWrapper : helpers::callback<context::Scalar>,
                             bp::wrapper<helpers::callback<context::Scalar>>
    {
      void call()
      {
        this->get_override("call")();
      }
    };

    void exposeCallbacks()
    {
      using context::Scalar;
      using callback_t = helpers::callback<Scalar>;

      bp::class_<CallbackWrapper, shared_ptr<CallbackWrapper>, boost::noncopyable>(
        "BaseCallback", "Base callback for solvers.", bp::no_init)
        .def("call", bp::pure_virtual(&callback_t::call), bp::args("self", "workspace", "results"))
        ;

      {
        using history_storage_t = decltype(helpers::history_callback<Scalar>::storage);

        bp::scope in_history = bp::class_<helpers::history_callback<Scalar>, bp::bases<callback_t>>(
          "HistoryCallback", "Store the history of solver's variables.",
          bp::init<bool, bool, bool>((
                      bp::arg("store_pd_vars") = true
                    , bp::arg("store_values") = true
                    , bp::arg("store_residuals") = true
                   ))
          )
          .def_readonly("storage", &helpers::history_callback<Scalar>::storage);

        bp::class_<history_storage_t>("_history_storage", bp::no_init)
          .def_readonly("xs", &history_storage_t::xs)
          .def_readonly("lams", &history_storage_t::lams)
          .def_readonly("values", &history_storage_t::values)
          .def_readonly("prim_infeas", &history_storage_t::prim_infeas)
          .def_readonly("dual_infeas", &history_storage_t::dual_infeas)
          ;
      }
    }    
  } // namespace python
} // namespace lienlp
