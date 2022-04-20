import pinocchio as pin
import pinocchio.casadi as cpin

import numpy as np
import casadi as cas
import lienlp

from lienlp.manifolds import MultibodyPhaseSpace, EuclideanSpace
from examples.utils import CasadiFunction

from meshcat_utils import display_trajectory, ForceDraw, VIDEO_CONFIG_DEFAULT

import example_robot_data as erd
from tap import Tap

import matplotlib.pyplot as plt


class Args(Tap):
    view: bool = False
    num_replay: int = 3
    record: bool = False


robot = erd.load("double_pendulum")
model = robot.model
model.lowerPositionLimit[:] = -2 * np.pi
model.upperPositionLimit[:] = +2 * np.pi

Tf = 1.5
dt = 0.03
nsteps = int(Tf / dt)
Tf = nsteps * dt
print("Time horizon: {:.3g}".format(Tf))
print("Time step   : {:.3g}".format(dt))

nq = model.nq
B = np.array([[0.], [1.]])
nu = B.shape[1]

u_bound = 2.

xspace = MultibodyPhaseSpace(model)
pb_space = EuclideanSpace(nsteps * nu + (nsteps + 1) * (xspace.nx))


class MyCallback(lienlp.BaseCallback):

    def __init__(self):
        super().__init__()

    def call(self, workspace: lienlp.Workspace, results: lienlp.Results):
        return
        with np.printoptions(precision=1, linewidth=250):
            print("JACOBIANS (callback):\n{}".format(workspace.jacobians_data))


args = Args().parse_args()
rdata = model.createData()
print(args)
VIEWER = args.view

if VIEWER:
    vizer: pin.visualize.MeshcatVisualizer = pin.visualize.MeshcatVisualizer(model, robot.collision_model, robot.visual_model)
    vizer.initViewer(loadModel=True)
    vizer.display(pin.neutral(model))
    vizer.viewer.open()
    drawer = ForceDraw(vizer)
    drawer.set_bg()
    drawer.set_cam_angle_preset('acrobot')


def make_dynamics_expression(cmodel: cpin.Model, cdata: cpin.Data, x0, cxs, cus):
    resdls = [cxs[0] - x0]
    for t in range(nsteps):
        q = cxs[t][:nq]
        v = cxs[t][nq:]
        tau = B @ cus[t]
        acc = cpin.aba(cmodel, cdata, q, v, tau)
        vnext = v + dt * acc
        qnext = cpin.integrate(cmodel, q, dt * vnext)
        xnext = cas.vertcat(qnext, vnext)
        resdls.append(cxs[t + 1] - xnext)
    expression = cas.vertcat(*resdls)
    return expression


class MultipleShootingProblem:
    """Multiple-shooting formulation."""
    def __init__(self, x0, xtarget):
        self.cmodel = cpin.Model(model)
        self.cdata = self.cmodel.createData()
        cxs = [cas.SX.sym("x%i" % i, xspace.nx) for i in range(nsteps + 1)]
        cus = [cas.SX.sym("u%i" % i, nu) for i in range(nsteps)]
        cX_s = cas.vertcat(*cxs)
        cU_s = cas.vertcat(*cus)

        cXU_s = cas.vertcat(cX_s, cU_s)

        w_u = 1e-2
        w_x = 1e-2
        w_term = 1e-1 * np.ones(xspace.ndx)
        w_term[2:] = 0.
        ferr = cxs[nsteps] - xtarget
        cost_expression = (
            0.5 * w_x * dt * cas.dot(cX_s, cX_s) +
            0.5 * w_u * dt * cas.dot(cU_s, cU_s) +
            0.5 * cas.dot(ferr, w_term * ferr))

        self.cost_fun = CasadiFunction(pb_space.nx, pb_space.ndx, cost_expression, cXU_s)

        x0 = cas.SX(x0)
        expression = make_dynamics_expression(self.cmodel, self.cdata, x0, cxs, cus)
        self.dynamics_fun = CasadiFunction(pb_space.nx, pb_space.ndx, expression, cXU_s)

        control_bounds_ = []
        for t in range(nsteps):
            control_bounds_.append(cus[t] - u_bound)
            control_bounds_.append(-cus[t] - u_bound)
        control_expr = cas.vertcat(*control_bounds_)
        self.control_bound_fun = CasadiFunction(pb_space.nx, pb_space.ndx, control_expr, cXU_s)


x0 = xspace.neutral()
x0[0] = np.pi
xtarget = xspace.neutral()


print("Initial:", x0)
print("Final  :", xtarget)

xu_init = pb_space.neutral()
probdef = MultipleShootingProblem(x0, xtarget)
cost_fun = lienlp.costs.CostFromFunction(probdef.cost_fun)
dynamical_constraint = lienlp.constraints.EqualityConstraint(probdef.dynamics_fun)
bound_constraint = lienlp.constraints.NegativeOrthant(probdef.control_bound_fun)

print("Cost : {}".format(probdef.cost_fun))
print("Dyn  : {}".format(probdef.dynamics_fun))
print("Bound: {}".format(probdef.control_bound_fun))

constraints_ = []
constraints_.append(dynamical_constraint)
constraints_.append(bound_constraint)
prob = lienlp.Problem(cost_fun, constraints_)

print("No. of variables  :", pb_space.nx)
print("No. of constraints:", prob.total_constraint_dim)
workspace = lienlp.Workspace(pb_space.nx, pb_space.ndx, prob)
results = lienlp.Results(pb_space.nx, prob)

callback = lienlp.HistoryCallback()
callback2 = MyCallback()
tol = 1e-4
rho_init = 0.
mu_init = 0.05
solver = lienlp.Solver(pb_space, prob, mu_init=mu_init, rho_init=rho_init, tol=tol, verbose=lienlp.VERBOSE)
solver.register_callback(callback)
solver.register_callback(callback2)
solver.maxiters = 500
solver.use_gauss_newton = True

lams0 = [np.zeros(cs.nr) for cs in constraints_]
flag = solver.solve(workspace, results, xu_init, lams0)

print("Results struct:\n{}".format(results))
prim_errs = callback.storage.prim_infeas
dual_errs = callback.storage.dual_infeas

xus_opt = results.xopt
xs_opt_flat = xus_opt[:(nsteps + 1) * xspace.nx]
us_opt_flat = xus_opt[(nsteps + 1) * xspace.nx:]
us_opt = us_opt_flat.reshape(nsteps, -1)
xs_opt = xs_opt_flat.reshape(nsteps + 1, -1)
qs_opt = xs_opt[:, :model.nq]
vs_opt = xs_opt[:, model.nq:]
print("X shape:", xs_opt.shape)


plt.style.use("seaborn-ticks")
plt.rcParams['lines.linewidth'] = 1.
plt.rcParams['axes.linewidth'] = 1.

times = np.linspace(0., Tf, nsteps + 1)
labels_ = ["$x_{%i}$" % i for i in range(model.nq)]

fig, axes = plt.subplots(1, 3, figsize=(10.8, 4.8))
axes: list[plt.Axes]

plt.sca(axes[0])
hlines_style = dict(alpha=.7, ls='-', lw=2, zorder=-1)
lines = plt.plot(times, qs_opt)
cols_ = [li.get_color() for li in lines]
labels_ = ["$q_{0}$".format(i) for i in range(model.nq)]
hlines = plt.hlines(xtarget[:model.nq], *times[[0, -1]], colors=cols_, **hlines_style)

plt.legend(labels_)
plt.xlabel("Time $t$")
plt.title("Configuration $q$")

plt.sca(axes[1])
plt.plot(times[:-1], us_opt)
plt.hlines((-u_bound, u_bound), *times[[0, -2]], colors='k', **hlines_style)
plt.xlabel("Time $t$")
plt.title("Controls $u$")

ax0 = axes[2]


def plot_pd_errs():
    ax0.plot(prim_errs, c='tab:blue')
    ax0.set_xlabel("Iterations")
    col2 = "tab:orange"
    ax0.plot(dual_errs, c=col2)
    ax0.spines['top'].set_visible(False)
    ax0.spines['right'].set_color(col2)
    ax0.yaxis.label.set_color(col2)
    ax0.set_yscale("log")
    ax0.legend(["Primal error $p$", "Dual error $d$"])
    ax0.set_title("Solver primal-dual residuals")


plot_pd_errs()

it_list = [1, 10, 20, 30]
it_list = [i for i in it_list if i < results.numiters]
for it in it_list:
    ls_alphas = callback.storage.ls_alphas[it].copy()
    ls_values = callback.storage.ls_values[it].copy()
    if len(ls_alphas) == 0:
        continue
    soidx = np.argsort(ls_alphas)
    ls_alphas = ls_alphas[soidx]
    ls_values = ls_values[soidx]
    plt.figure()
    plt.plot(ls_alphas, ls_values)
    d1 = callback.storage.d1_s[it]
    plt.plot(ls_alphas, ls_values[0] + ls_alphas * d1)
    plt.plot(ls_alphas, ls_values[0] + solver.armijo_c1 * ls_alphas * d1, ls='--')
    plt.title("Iteration %d" % it)
plt.show()


if VIEWER:

    drawer.set_cam_angle_preset('acrobot')
    allimgs = []
    for _ in range(args.num_replay):
        imgs = display_trajectory(vizer, drawer, xs_opt, us_opt,
                                  record=args.record, wait=dt)
        if imgs is not None:
            allimgs.extend(imgs)

    if args.record:
        import imageio
        imageio.mimwrite("double_pendulum.mp4", ims=allimgs, fps=1. / dt,
                         **VIDEO_CONFIG_DEFAULT)
