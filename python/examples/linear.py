import numpy as np
from lienlp import LinearResidual, EqualityConstraint

A = np.random.randn(2, 2)
b = np.random.randn(2)
x0 = np.linalg.solve(A, -b)

resdl = LinearResidual(A, b)

assert np.allclose(resdl.computeJacobian(x0), A)
assert np.allclose(resdl(x0), 0.)
assert np.allclose(resdl(np.zeros_like(b)), b)

print("x0:", x0)

cstr = EqualityConstraint(resdl)

print(cstr.projection(x0), "should be zero")
print(cstr.normalConeProjection(x0), "should be x0")